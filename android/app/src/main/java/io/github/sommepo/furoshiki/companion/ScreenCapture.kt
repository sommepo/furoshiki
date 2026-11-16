// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.graphics.Rect
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.util.Log
import android.view.Display
import io.github.sommepo.furoshiki.R
import io.github.sommepo.furoshiki.ocr.OcrFrames

/**
 * The screen another app's game is on, captured (MediaProjection) for OCR and for cards' pictures:
 * the device's main display, where the companion starts the game, while Furoshiki stands on the
 * second one ([CompanionActivity]). Android asks the player first, each time a capture begins.
 *
 * A foreground service, as Android wants of a capture; it runs in the game's process, beside the
 * reading session, and [frames] is how the rest of that process reads the screen.
 */
class ScreenCapture : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val result = intent?.getIntExtra(EXTRA_RESULT, 0) ?: 0
        val data: Intent? = if (Build.VERSION.SDK_INT >= 33) intent?.getParcelableExtra(EXTRA_DATA, Intent::class.java)
        else @Suppress("DEPRECATION") intent?.getParcelableExtra(EXTRA_DATA)
        goForeground()
        if (data == null) {
            stopSelf()
            return START_NOT_STICKY
        }
        val manager = getSystemService(MediaProjectionManager::class.java)
        val projection = runCatching { manager.getMediaProjection(result, data) }.getOrNull()
        if (projection == null) {
            Log.w("Furoshiki", "Companion: no screen capture")
            stopSelf()
            return START_NOT_STICKY
        }
        frames.begin(this, projection)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        frames.end()
        super.onDestroy()
    }

    private fun goForeground() {
        val notifications = getSystemService(NotificationManager::class.java)
        notifications.createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.companion_channel), NotificationManager.IMPORTANCE_LOW)
        )
        val notification = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_furoshiki_30)
            .setContentTitle(getString(R.string.companion_capturing))
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        else startForeground(NOTIFICATION, notification)
    }

    /** The captured screen as OCR reads it: the newest frame, copied when asked for. */
    class Frames : OcrFrames {
        private val main = Handler(Looper.getMainLooper())
        private var thread: HandlerThread? = null
        private var projection: MediaProjection? = null
        private var display: VirtualDisplay? = null
        private var reader: ImageReader? = null

        @Volatile
        override var width = 0
            private set

        @Volatile
        override var height = 0
            private set

        /** Called on the main thread when the capture has begun (true) or has ended (false). */
        var onChanged: ((Boolean) -> Unit)? = null

        val capturing: Boolean get() = reader != null

        fun begin(context: Context, projection: MediaProjection) {
            end()
            val screen = context.getSystemService(DisplayManager::class.java).getDisplay(Display.DEFAULT_DISPLAY)
            // The main display as it is held (the Thor's upper one: 1920 by 1080). Its mode is its
            // own; its metrics, asked from a process whose window is on the second display, were
            // the second display's on the Thor.
            val mode = screen.mode
            val turned = screen.rotation == android.view.Surface.ROTATION_90 || screen.rotation == android.view.Surface.ROTATION_270
            val across = if (turned) mode.physicalHeight else mode.physicalWidth
            val down = if (turned) mode.physicalWidth else mode.physicalHeight
            // No larger than OCR needs.
            val scale = minOf(1f, CAPTURE_WIDTH.toFloat() / maxOf(across, down))
            val w = (across * scale).toInt().coerceAtLeast(2) and 1.inv()
            val h = (down * scale).toInt().coerceAtLeast(2) and 1.inv()
            val density = context.resources.displayMetrics.densityDpi
            val worker = HandlerThread("ScreenCapture").also { it.start() }
            val handler = Handler(worker.looper)
            val images = ImageReader.newInstance(w, h, PixelFormat.RGBA_8888, 2)
            projection.registerCallback(object : MediaProjection.Callback() {
                override fun onStop() {
                    main.post { end() }
                }
            }, handler)
            this.projection = projection
            thread = worker
            reader = images
            width = w
            height = h
            display = projection.createVirtualDisplay(
                "Furoshiki companion", w, h, density,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, images.surface, null, handler
            )
            Log.i("Furoshiki", "Companion: capturing the screen at ${w}x$h (mode ${mode.physicalWidth}x${mode.physicalHeight}, rotation ${screen.rotation})")
            main.post { onChanged?.invoke(true) }
        }

        fun end() {
            val was = reader != null
            runCatching { display?.release() }
            runCatching { reader?.close() }
            runCatching { projection?.stop() }
            thread?.quitSafely()
            display = null
            reader = null
            projection = null
            thread = null
            width = 0
            height = 0
            if (was) main.post { onChanged?.invoke(false) }
        }

        override fun picture(): Rect? = if (width > 0 && height > 0) Rect(0, 0, width, height) else null

        override fun copy(maxWidth: Int, done: (Bitmap?) -> Unit) {
            val images = reader
            val worker = thread
            if (images == null || worker == null) {
                done(null)
                return
            }
            Handler(worker.looper).post {
                val bitmap = runCatching { latest(images, maxWidth) }.getOrNull()
                main.post { done(bitmap) }
            }
        }

        /** The newest frame, without the rows' padding, at most [maxWidth] wide. */
        private var kept: Bitmap? = null

        private fun latest(images: ImageReader, maxWidth: Int): Bitmap? {
            val image = images.acquireLatestImage()
            if (image != null) {
                image.use {
                    val plane = it.planes[0]
                    val pixelStride = plane.pixelStride
                    val padded = plane.rowStride / pixelStride
                    val full = Bitmap.createBitmap(padded, it.height, Bitmap.Config.ARGB_8888)
                    full.copyPixelsFromBuffer(plane.buffer)
                    val exact = if (padded == it.width) full else Bitmap.createBitmap(full, 0, 0, it.width, it.height).also { full.recycle() }
                    kept?.recycle()
                    kept = exact
                }
            }
            // The screen has not changed since the last frame: that frame is still what it shows.
            val frame = kept ?: return null
            val scale = minOf(1f, maxWidth.toFloat() / frame.width)
            return if (scale >= 1f) frame.copy(Bitmap.Config.ARGB_8888, false)
            else Bitmap.createScaledBitmap(frame, (frame.width * scale).toInt().coerceAtLeast(1), (frame.height * scale).toInt().coerceAtLeast(1), true)
        }
    }

    companion object {
        const val EXTRA_RESULT = "result"
        const val EXTRA_DATA = "data"
        private const val CHANNEL = "companion"
        private const val NOTIFICATION = 7
        private const val CAPTURE_WIDTH = 1920

        /** The one capture of this process. */
        val frames = Frames()

        fun start(context: Context, result: Int, data: Intent) {
            context.startForegroundService(
                Intent(context, ScreenCapture::class.java).putExtra(EXTRA_RESULT, result).putExtra(EXTRA_DATA, data)
            )
        }

        fun stop(context: Context) {
            frames.end()
            context.stopService(Intent(context, ScreenCapture::class.java))
        }
    }
}
