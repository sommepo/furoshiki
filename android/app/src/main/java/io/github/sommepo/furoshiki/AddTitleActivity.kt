// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import io.github.sommepo.furoshiki.companion.AppTitles

/**
 * *Add a game*'s name, asked on the second display of a device with two (a new user, 2026-11-19:
 * "when doing add game you should be able to type on the bottom screen"): a screen of its own
 * there, because only an activity's window gets the keyboard on that display (a dialog made from
 * the second display's page opened on the first, and a Presentation there was never given the
 * keyboard: both seen on the Thor). The shelf reads its titles again when this has added one.
 */
class AddTitleActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val app = intent.getStringExtra(EXTRA_APP).orEmpty()
        val appActivity = intent.getStringExtra(EXTRA_ACTIVITY).orEmpty()
        val label = intent.getStringExtra(EXTRA_LABEL).orEmpty()
        val density = resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xFF101A3A.toInt())
            setPadding(dp(28), dp(12), dp(28), dp(8))
        }
        fun words(text: String, size: Float, colour: Int) = TextView(this).apply {
            this.text = text
            textSize = size
            setTextColor(colour)
        }
        val name = EditText(this).apply {
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT
            imeOptions = EditorInfo.IME_ACTION_DONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI
            textSize = 22f
            setTextColor(0xFFECE5D3.toInt())
        }
        fun done() {
            val title = name.text.toString().trim()
            if (title.isNotEmpty() && app.isNotEmpty()) {
                AppTitles.add(this, title, app, appActivity, label)
            }
            finish()
        }
        name.setOnEditorActionListener { _, _, _ -> done(); true }
        fun key(text: Int, filled: Boolean, act: () -> Unit) = TextView(this).apply {
            setText(text)
            isAllCaps = true
            textSize = 15f
            letterSpacing = .12f
            gravity = Gravity.CENTER
            setTextColor(if (filled) 0xFF101A3A.toInt() else 0xFFECE5D3.toInt())
            background = GradientDrawable().apply {
                setColor(if (filled) 0xFFD9472B.toInt() else Color.TRANSPARENT)
                setStroke(1, if (filled) 0xFFD9472B.toInt() else 0x66ECE5D3)
            }
            setOnClickListener { act() }
        }
        val keys = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        keys.addView(key(R.string.add_title_cancel, false) { finish() }, LinearLayout.LayoutParams(0, dp(44), 1f).apply { marginEnd = dp(10) })
        keys.addView(key(R.string.add_title_add, true) { done() }, LinearLayout.LayoutParams(0, dp(44), 2f))
        column.addView(words(getString(R.string.add_title_name), 17f, 0xFFECE5D3.toInt()))
        column.addView(words(label, 12f, 0xFF8A93B5.toInt()))
        column.addView(name, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(4); bottomMargin = dp(8) })
        column.addView(keys)
        setContentView(column)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE or WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        name.requestFocus()
        name.postDelayed({ getSystemService(InputMethodManager::class.java)?.showSoftInput(name, InputMethodManager.SHOW_IMPLICIT) }, 250)
    }

    /** Added or not, the shelf is told: its page on this display comes back, and reads its titles again. */
    override fun onDestroy() {
        sendBroadcast(Intent(Places.SCANNED).setPackage(packageName).putExtra(ScanService.EXTRA_LISTED, true).putExtra(EXTRA_DONE, true))
        super.onDestroy()
    }

    companion object {
        const val EXTRA_DONE = "title_asked"
        const val EXTRA_APP = "app"
        const val EXTRA_ACTIVITY = "activity"
        const val EXTRA_LABEL = "label"
    }
}
