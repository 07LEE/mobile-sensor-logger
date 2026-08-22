package com.sensor.logger;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

public class RecordingService extends Service {
    public static final String ACTION_START = "com.sensor.logger.action.START";
    public static final String ACTION_STOP = "com.sensor.logger.action.STOP";
    private static final String CHANNEL_ID = "sensor_logger_recording_channel";
    private static final int NOTIFICATION_ID = 1001;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // A null Intent is Android redelivering this after the process — service
        // and native engine together, since they share one process — was killed
        // and restarted. There is no recording behind that restart for this
        // notification to represent, and native code calls back in to start the
        // service again if a recording actually resumes, so stopping here rather
        // than reposting "background recording active" avoids lying about it.
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }

        Notification notification = buildNotification();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }

        // Not START_STICKY: native code is what decides whether this service
        // should be running (see ADR 0007), so Android restarting it on its own
        // after a kill would only recreate the notification with no capture
        // engine behind it.
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Sensor Logger Recording",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Active background sensor and camera recording session");
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification() {
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
            .setContentTitle("Sensor Logger Recording")
            .setContentText("Background recording active")
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setOngoing(true)
            .build();
    }
}
