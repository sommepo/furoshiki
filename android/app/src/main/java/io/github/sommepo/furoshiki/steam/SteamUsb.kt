// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.steam

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log

/**
 * The Steam Controller (2026) over USB, wired or through its puck, read as the desktop's bridge
 * reads it (SteamPad): Android has no driver of its own for it, and without one the controller is
 * only its own keyboard and mouse. The interface that sends state reports (its report descriptor
 * has report 0x42) is taken from the system, its reports read on a thread of its own, the
 * controller's keyboard and mouse switched off every two seconds (its firmware brings them back),
 * the pads' ticks written back. What the reports say goes to [sink] on the main thread.
 *
 * One process at a time holds the controller: the screen in front starts this as it comes to the
 * front ([start]) and stops it as it goes ([stop]); Furoshiki's games play in a process of their
 * own. Permission is asked once (or given by the system's "open with" for a USB device, with
 * "always", from the manifest's filter).
 */
class SteamUsb(private val context: Context, private val sink: SteamPad.Sink, private val tuning: () -> SteamPad.Tuning = { SteamPad.Tuning() }) {
    private val usb = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val main = Handler(Looper.getMainLooper())
    private val open = HashMap<String, Opened>()
    private var started = false

    /** True while a controller sends its state. */
    @Volatile
    var connected = false
        private set

    private class Opened(val device: UsbDevice, val connection: UsbDeviceConnection) {
        val readers = ArrayList<Thread>()
        @Volatile
        var stopping = false
    }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val device = deviceOf(intent) ?: return
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> close(device.deviceName)
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> consider(device, ask = true)
                ACTION_PERMISSION -> if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) consider(device, ask = false)
            }
        }
    }

    fun start() {
        if (started) return
        started = true
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_PERMISSION)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, filter)
        }
        for (device in usb.deviceList.values) consider(device, ask = true)
        // Made-up reports for testing without the controller: `adb shell am broadcast -a
        // io.github.sommepo.furoshiki.STEAM_TEST_REPORT --es hex 42…` (only the shell and the system
        // may send it: DUMP). They go the same way as the controller's.
        val test = IntentFilter(ACTION_TEST_REPORT)
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(testReceiver, test, "android.permission.DUMP", null, Context.RECEIVER_EXPORTED)
        } else {
            context.registerReceiver(testReceiver, test, "android.permission.DUMP", null)
        }
    }

    private val testPad = SteamPad()
    private val testPending = ArrayList<() -> Unit>()
    private val testReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.getBooleanExtra("release", false)) {
                testPad.release(Collector(testPending, sink))
            } else {
                val hex = intent.getStringExtra("hex")?.filter { !it.isWhitespace() } ?: return
                val data = ByteArray(hex.length / 2) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
                testPad.tuning = tuning()
                if (!testPad.report(data, data.size, Collector(testPending, sink))) return
            }
            val batch = testPending.toList()
            testPending.clear()
            batch.forEach { it() } // already on the main thread
        }
    }

    fun stop() {
        if (!started) return
        started = false
        runCatching { context.unregisterReceiver(receiver) }
        runCatching { context.unregisterReceiver(testReceiver) }
        for (name in open.keys.toList()) close(name)
    }

    private fun consider(device: UsbDevice, ask: Boolean) {
        if (!started || !isSteamController(device) || device.deviceName in open) return
        if (!usb.hasPermission(device)) {
            if (ask) {
                val intent = Intent(ACTION_PERMISSION).setPackage(context.packageName)
                val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
                usb.requestPermission(device, PendingIntent.getBroadcast(context, 0, intent, flags))
            }
            return
        }
        val connection = usb.openDevice(device) ?: return
        val opened = Opened(device, connection)
        for (index in 0 until device.interfaceCount) {
            val face = device.getInterface(index)
            if (face.interfaceClass != UsbConstants.USB_CLASS_HID) continue
            if (!connection.claimInterface(face, true)) continue
            val input = endpoint(face, UsbConstants.USB_DIR_IN)
            if (input == null || !sendsState(connection, face)) {
                connection.releaseInterface(face)
                continue
            }
            val output = endpoint(face, UsbConstants.USB_DIR_OUT)
            opened.readers += Thread({ read(opened, face, input, output) }, "steam-controller-${face.id}").apply { start() }
        }
        if (opened.readers.isEmpty()) {
            connection.close()
            return
        }
        open[device.deviceName] = opened
    }

    private fun close(name: String) {
        val opened = open.remove(name) ?: return
        opened.stopping = true
        for (thread in opened.readers) runCatching { thread.join(600) }
        runCatching { opened.connection.close() }
        connected = open.isNotEmpty() && connected
    }

    /** One interface's reports, until the controller goes or this stops. */
    private fun read(opened: Opened, face: UsbInterface, input: UsbEndpoint, output: UsbEndpoint?) {
        val pad = SteamPad(tuning())
        val buffer = ByteArray(maxOf(input.maxPacketSize, 64))
        var lizardAt = 0L
        var talking = false
        var lastState = 0L
        val pending = ArrayList<() -> Unit>()
        val collect = Collector(pending, sink)
        try {
            while (!opened.stopping) {
                val got = opened.connection.bulkTransfer(input, buffer, buffer.size, 250)
                val now = SystemClock.uptimeMillis()
                if (got <= 0) {
                    // Off or out of range: let go of everything.
                    if (talking && now - lastState > 2000) {
                        talking = false
                        pad.release(collect)
                        flush(pending)
                    }
                    continue
                }
                val id = buffer[0].toInt() and 0xFF
                if (id == SteamPad.WIRELESS_REPORT && got >= 2 && buffer[1].toInt() == 1 && talking) {
                    talking = false
                    pad.release(collect)
                    flush(pending)
                    continue
                }
                pad.tuning = tuning()
                if (!pad.report(buffer, got, collect)) continue
                lastState = now
                if (!talking) {
                    talking = true
                    connected = true
                    Log.i(TAG, "Steam Controller on interface ${face.id}")
                }
                flush(pending)
                while (true) {
                    val pulse = pad.takePulse() ?: break
                    haptic(opened.connection, face, output, pulse)
                }
                if (now - lizardAt >= 2000) {
                    lizardAt = now
                    lizardOff(opened.connection, face)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Steam Controller reading stopped", e)
        } finally {
            if (talking) {
                pad.release(collect)
                flush(pending)
            }
            runCatching { opened.connection.releaseInterface(face) }
        }
    }

    /** What a report said, gathered on the reading thread and handed to [sink] on the main one at once. */
    private class Collector(val pending: MutableList<() -> Unit>, val target: SteamPad.Sink) : SteamPad.Sink {
        override fun button(button: SteamPad.Button, down: Boolean) { pending += { target.button(button, down) } }
        override fun axis(axis: SteamPad.Axis, value: Int) { pending += { target.axis(axis, value) } }
        override fun pointerTouch(touching: Boolean) { pending += { target.pointerTouch(touching) } }
        override fun pointerMove(dx: Int, dy: Int) { pending += { target.pointerMove(dx, dy) } }
        override fun pointerClick(left: Boolean, down: Boolean) { pending += { target.pointerClick(left, down) } }
        override fun wheel(amount: Int) { pending += { target.wheel(amount) } }
        override fun reportDone() { pending += { target.reportDone() } }
    }

    private fun flush(pending: MutableList<() -> Unit>) {
        if (pending.isEmpty()) return
        val batch = pending.toList()
        pending.clear()
        main.post { batch.forEach { it() } }
    }

    private fun lizardOff(connection: UsbDeviceConnection, face: UsbInterface) {
        val message = SteamPad.LIZARD_OFF.copyOf(SteamPad.FEATURE_LENGTH)
        // SET_REPORT of a feature report: its ID (1) in the value's low byte, the whole report as data.
        connection.controlTransfer(0x21, 0x09, (3 shl 8) or 1, face.id, message, message.size, 300)
    }

    private fun haptic(connection: UsbDeviceConnection, face: UsbInterface, output: UsbEndpoint?, pulse: SteamPad.Pulse) {
        if (output != null) {
            val message = SteamPad.hapticMessage(pulse, output.maxPacketSize)
            connection.bulkTransfer(output, message, message.size, 50)
        } else {
            val message = SteamPad.hapticMessage(pulse, 64)
            connection.controlTransfer(0x21, 0x09, (2 shl 8) or SteamPad.HAPTIC_REPORT, face.id, message, message.size, 50)
        }
    }

    companion object {
        private const val TAG = "SteamUsb"
        private const val ACTION_PERMISSION = "io.github.sommepo.furoshiki.STEAM_CONTROLLER_PERMISSION"
        const val ACTION_TEST_REPORT = "io.github.sommepo.furoshiki.STEAM_TEST_REPORT"

        fun isSteamController(device: UsbDevice): Boolean =
            device.vendorId == SteamPad.VENDOR && device.productId in SteamPad.PRODUCTS

        private fun endpoint(face: UsbInterface, direction: Int): UsbEndpoint? = (0 until face.endpointCount)
            .map { face.getEndpoint(it) }
            .firstOrNull { it.direction == direction && it.type == UsbConstants.USB_ENDPOINT_XFER_INT }

        /** Whether the interface's report descriptor has the state report (0x85 0x42: Report ID 0x42). */
        private fun sendsState(connection: UsbDeviceConnection, face: UsbInterface): Boolean {
            val descriptor = ByteArray(4096)
            val got = connection.controlTransfer(0x81, 0x06, 0x22 shl 8, face.id, descriptor, descriptor.size, 500)
            if (got <= 1) return false
            return hasStateReport(descriptor, got)
        }

        /** True when a report descriptor names report 0x42. */
        fun hasStateReport(descriptor: ByteArray, size: Int = descriptor.size): Boolean =
            (0 until size - 1).any { (descriptor[it].toInt() and 0xFF) == 0x85 && (descriptor[it + 1].toInt() and 0xFF) == SteamPad.STATE_REPORT }

        @Suppress("DEPRECATION")
        private fun deviceOf(intent: Intent): UsbDevice? = if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
    }
}
