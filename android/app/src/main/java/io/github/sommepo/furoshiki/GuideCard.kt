package io.github.sommepo.furoshiki

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import io.github.sommepo.furoshiki.reader.ReaderSettings
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlin.concurrent.thread

/**
 * The guide's next step over the game, in its top left corner (the desktop's GuideOverlay): the
 * first step not ticked yet of the section the checklist stands at, its note, how far the section
 * is. Shown and hidden from the menu over the game or with Select and L1; Select and R1, or a long
 * press on the card, ticks the step shown. Asked of Furochibi's service, which the paired save
 * sync service names (GET /v1/pair), every half minute while shown. The dev line's: a device not
 * paired with a save sync service knows no guide service, and the menu does not offer it.
 */
class GuideCard(private val activity: Activity, private val gameId: String) {
    private val main = Handler(Looper.getMainLooper())
    private val density = activity.resources.displayMetrics.density
    private var service: String? = null
    private var slug = ""
    private var stepId = ""
    private var asking = false

    private val head = text(11f, MUTED, bold = true)
    private val step = text(16f, INK, bold = false)
    private val note = text(12.5f, MUTED, bold = false)
    private val foot = text(10.5f, MUTED, bold = false)

    val view: LinearLayout = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        val pad = (14 * density).toInt()
        setPadding(pad + (4 * density).toInt(), pad, pad, pad)
        background = GradientDrawable().apply {
            cornerRadius = 10 * density
            setColor(GROUND)
        }
        addView(head)
        addView(step, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            topMargin = (6 * density).toInt()
        })
        addView(note)
        addView(foot, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            topMargin = (8 * density).toInt()
        })
        visibility = View.GONE
        // A long press ticks the step shown; a tap goes through to nothing (the card is small).
        setOnLongClickListener { done(); true }
    }

    private val again = object : Runnable {
        override fun run() {
            ask()
            main.postDelayed(this, 30_000)
        }
    }

    /** Adds the card to the game's frame, over the picture, in the top left corner. */
    fun addTo(frame: FrameLayout) {
        val width = (activity.resources.displayMetrics.widthPixels * .34f).toInt().coerceIn((300 * density).toInt(), (520 * density).toInt())
        frame.addView(view, FrameLayout.LayoutParams(width, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START).apply {
            leftMargin = (16 * density).toInt()
            topMargin = (16 * density).toInt()
        })
    }

    val shown: Boolean get() = view.visibility == View.VISIBLE

    /** Whether a guide can be asked for at all on this device. */
    fun offered(): Boolean =
        false

    fun toggle() {
        if (shown) {
            hide()
            return
        }
        view.visibility = View.VISIBLE
        if (slug.isEmpty()) say(activity.getString(R.string.guide_asking))
        main.removeCallbacks(again)
        main.post(again)
    }

    fun hide() {
        view.visibility = View.GONE
        main.removeCallbacks(again)
    }

    private fun say(text: String) {
        head.text = activity.getString(R.string.guide_head)
        step.text = text
        step.setTextColor(MUTED)
        note.visibility = View.GONE
        foot.text = ""
    }

    private fun ask() {
        if (asking) return
        asking = true
        thread(name = "Guide") {
            val answer = runCatching {
                val at = service ?: (Services.of(activity, "furochibi").ifEmpty { null } ?: findService()).also { service = it }
                    ?: return@runCatching null to activity.getString(R.string.guide_no_service)
                val json = JSONObject(get("$at/api/guide?game_id=" + URLEncoder.encode(gameId, "UTF-8")))
                json to ""
            }.getOrElse {
                android.util.Log.w("GuideCard", "the guide service did not answer", it)
                null to activity.getString(R.string.guide_no_answer)
            }
            main.post {
                asking = false
                val json = answer.first
                if (json == null || json.optString("slug").isEmpty()) {
                    slug = ""
                    say(answer.second.ifEmpty { activity.getString(R.string.guide_none) })
                } else {
                    show(json)
                }
            }
        }
    }

    private fun show(json: JSONObject) {
        slug = json.optString("slug")
        val section = json.optJSONObject("section")
        val title = section?.optString("title").orEmpty()
        val sub = section?.optString("sub").orEmpty()
        head.text = (if (sub.isEmpty()) title else "$title · $sub").uppercase()
        val steps = json.optJSONArray("steps")
        var next: JSONObject? = null
        for (i in 0 until (steps?.length() ?: 0)) {
            val each = steps!!.getJSONObject(i)
            if (!each.optBoolean("done")) {
                next = each
                break
            }
        }
        stepId = next?.optString("id").orEmpty()
        step.setTextColor(if (next != null) INK else MUTED)
        step.text = next?.optString("text")?.trim() ?: activity.getString(R.string.guide_section_done)
        val noteText = next?.optString("note")?.trim().orEmpty()
        note.text = noteText
        note.visibility = if (noteText.isEmpty()) View.GONE else View.VISIBLE
        val done = json.optInt("done")
        val total = json.optInt("total")
        foot.text = (if (total > 0) "$done / $total · " else "") + activity.getString(R.string.guide_keys)
    }

    /** Ticks the step shown and asks for where the checklist stands now. */
    fun done() {
        val at = service ?: return
        if (!shown || slug.isEmpty() || stepId.isEmpty()) return
        val id = stepId
        stepId = ""
        step.setTextColor(MUTED)
        thread(name = "GuideTick") {
            runCatching {
                post("$at/api/guide/" + URLEncoder.encode(slug, "UTF-8") + "/tick", JSONObject().put("id", id).put("done", true).toString())
            }
            main.post { ask() }
        }
    }

    /** Furochibi's address, as the paired save sync service names it. */
    private fun findService(): String? {
        return null
    }

    private fun get(address: String, bearer: String = ""): String {
        val connection = URL(address).openConnection() as HttpURLConnection
        connection.connectTimeout = 6000
        connection.readTimeout = 20000
        if (bearer.isNotEmpty()) connection.setRequestProperty("Authorization", "Bearer $bearer")
        try {
            if (connection.responseCode !in 200..299) error("HTTP ${connection.responseCode}")
            return connection.inputStream.bufferedReader().use { it.readText() }
        } finally {
            connection.disconnect()
        }
    }

    private fun post(address: String, body: String) {
        val connection = URL(address).openConnection() as HttpURLConnection
        connection.connectTimeout = 6000
        connection.readTimeout = 20000
        connection.requestMethod = "POST"
        connection.doOutput = true
        connection.setRequestProperty("Content-Type", "application/json")
        try {
            connection.outputStream.use { it.write(body.toByteArray()) }
            if (connection.responseCode !in 200..299) error("HTTP ${connection.responseCode}")
        } finally {
            connection.disconnect()
        }
    }

    private fun text(size: Float, colour: Int, bold: Boolean) = TextView(activity).apply {
        setTextSize(TypedValue.COMPLEX_UNIT_SP, size)
        setTextColor(colour)
        if (bold) {
            typeface = Typeface.DEFAULT_BOLD
            letterSpacing = .08f
        }
    }

    companion object {
        // Furoshiki's Kon: indigo glass, cream type.
        private val GROUND = Color.argb(225, 16, 26, 58)
        private val INK = Color.rgb(236, 229, 211)
        private val MUTED = Color.rgb(138, 147, 181)
    }
}
