package com.example.appvideo;

import java.io.IOException;
import java.nio.channels.ClosedByInterruptException;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.app.Notification;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.nio.channels.ReadableByteChannel;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;

public class DownloadService extends Service {
    public static final String ACTION_START = "com.example.testhttp.action.START";
    public static final String ACTION_STOP = "com.example.testhttp.action.STOP";

    private static final String CHANNEL_ID = "download_channel";
    private static final int NOTIFICATION_ID = 1;

    private volatile boolean isDownloading = false;
    private Thread downloadThread;
    private NotificationManager notificationManager;
    private NotificationCompat.Builder notificationBuilder;
    private Call ongoingCall;


    private Uri videoFileUri;
    private String fileName;
    private final OkHttpClient httpClient = new OkHttpClient.Builder()
            .connectTimeout(30, TimeUnit.SECONDS)
            .readTimeout(60, TimeUnit.SECONDS)
            .writeTimeout(60, TimeUnit.SECONDS)
            .build();

    @Override
    public void onCreate() {
        super.onCreate();
        notificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent.getAction();
        //The user wants to stop the download
        if (ACTION_STOP.equals(action)) {
            //Log.d("app", "Richiesto arresto del servizio");
            isDownloading = false;
            stopSelf();
            notificationManager.cancel(NOTIFICATION_ID);
            notifyDownloadComplete(false);
            return START_NOT_STICKY;
        }

        //The user wants to start the download
        String videoUrl = intent.getStringExtra("videoUrl");
        fileName = intent.getStringExtra("fileName");
        if (videoUrl != null && !isDownloading) {
            //Log.d("app", "Avvio download da URL: " + videoUrl);
            startDownload(videoUrl);
        } else if (isDownloading) {
            //Log.d("app", "Download già in corso, ignoro nuova richiesta");
        }

        return START_NOT_STICKY;
    }


    @Override
    public void onDestroy() {
        isDownloading = false;

        //Cancel pending request if present, to avoid exception
        if (ongoingCall != null) {
            ongoingCall.cancel();
        }

        //Wait for the download thread to stop
        if (downloadThread != null && downloadThread.isAlive()) {
            try {
                downloadThread.join();
            } catch (InterruptedException ignored) {}
        }

        //Delete notification
        stopForeground(true);
        notificationManager.cancel(NOTIFICATION_ID);
        super.onDestroy();
    }


    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Download", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Download progress");
            notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(channel);
        }
    }

    private void startDownload(String videoUrl) {
        isDownloading = true;
        //Log.d("app", "Inizio download su thread separato");

        videoFileUri = createVideoFileInDownloads(fileName);
        if (videoFileUri == null) {
            stopSelf();
            return;
        }

        downloadThread = new Thread(() -> {
            long downloadedBytes = 0;
            long totalBytes = -1;
            ContentResolver resolver = getContentResolver();

            try {
                while (isDownloading) {
                    String rangeHeader = "bytes=" + downloadedBytes + "-"; //Ask from the current point. The server can choose the amound of data to send
                    Request request = new Request.Builder()
                            .url(videoUrl)
                            .addHeader("Range", rangeHeader)
                            .build();

                    ongoingCall = httpClient.newCall(request);
                    try (Response response = ongoingCall.execute()) {
                        if (!response.isSuccessful()) {
                            Log.e("DownloadService", "Errore nella risposta: " + response.code());
                            break;
                        }

                        //Get total bytes if not already known
                        if (totalBytes == -1) {
                            totalBytes = getTotalBytesFromResponse(response);
                            Log.d("app", "Total bytes: " + totalBytes);
                        }

                        try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(videoFileUri, "rw");
                             FileChannel fileChannel = new FileOutputStream(pfd.getFileDescriptor()).getChannel()) {

                            if (pfd == null) {
                                Log.e("app", "Failed to get ParcelFileDescriptor");
                                break;
                            }

                            //Position the channel at the correct offset
                            fileChannel.position(downloadedBytes);

                            ResponseBody body = response.body();
                            if (body == null) {
                                Log.e("app", "Response body is null");
                                break;
                            }

                            try (InputStream input = body.byteStream();
                                 ReadableByteChannel inputChannel = Channels.newChannel(input)) {

                                ByteBuffer buffer = ByteBuffer.allocateDirect(1024 * 1024); //1 MB buffer
                                long lastUpdate = System.currentTimeMillis();

                                while (isDownloading) {
                                    buffer.clear();
                                    int bytesRead = inputChannel.read(buffer);
                                    if (bytesRead == -1) break;

                                    buffer.flip(); //Prepare buffer for reading
                                    while (buffer.hasRemaining()) {
                                        downloadedBytes += fileChannel.write(buffer);
                                    }

                                    //Update progress
                                    long now = System.currentTimeMillis();
                                    if (now - lastUpdate > 500 && totalBytes > 0) { //500 ms delay
                                        int progress = (int) ((downloadedBytes * 100) / totalBytes);
                                        //Log.d("app", "Progress: " + progress + "% (" + downloadedBytes + "/" + totalBytes + ")");
                                        updateNotification(progress);
                                        lastUpdate = now;
                                    }
                                }
                            }
                        }

                        // Check if download is complete
                        if (totalBytes > 0 && downloadedBytes >= totalBytes) {
                            //Log.d("app", "Download completato");
                            updateNotification(100);
                            break;
                        }
                    }
                }


            } catch (ClosedByInterruptException e) {
                //Log.i("app", "Download interrotto dall'utente.");
            }catch (IOException e) {
                if (ongoingCall.isCanceled()) {
                    //Log.d("app", "Download annullato.");
                }
            }catch (Exception e) {
                //Log.e("app", "Errore durante il download", e);
            } finally {
                //Cleanup
                stopForeground(true);
                notificationManager.cancel(NOTIFICATION_ID);
                stopSelf();
                notifyDownloadComplete(downloadedBytes >= totalBytes);
            }
        });

        //Start the download
        startForeground(NOTIFICATION_ID, buildNotification(0));
        downloadThread.start();
    }

    private long getTotalBytesFromResponse(Response response) {
        //Try from Content-Range header first
        String contentRange = response.header("Content-Range");
        if (contentRange != null && contentRange.contains("/")) {
            try {
                return Long.parseLong(contentRange.substring(contentRange.lastIndexOf('/') + 1));
            } catch (NumberFormatException ignored) {}
        }

        //Fall back to content-length
        ResponseBody body = response.body();
        if (body != null) {
            return body.contentLength();
        }

        return -1;
    }

    private Uri createVideoFileInDownloads(String filename) {
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, filename);
        values.put(MediaStore.Downloads.MIME_TYPE, "video/mp4");
        values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);

        ContentResolver resolver = getContentResolver();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        }
        return null;
    }

    private Notification buildNotification(int progress) {
        if (notificationBuilder == null) {
            notificationBuilder = new NotificationCompat.Builder(this, CHANNEL_ID)
                    .setContentTitle("Download in corso")
                    .setSmallIcon(android.R.drawable.stat_sys_download)
                    .setOnlyAlertOnce(true)
                    .setOngoing(true)
                    .setProgress(100, progress, false)
                    .addAction(android.R.drawable.ic_delete, "Annulla", createStopIntent());
        } else {
            notificationBuilder.setProgress(100, progress, false);
        }
        notificationBuilder.setContentText(progress == 100 ? "Download completato" : "Scaricando: " + progress + "%");
        return notificationBuilder.build();
    }


    private void updateNotification(int progress) {
        Notification notification = buildNotification(progress);
        notificationManager.notify(NOTIFICATION_ID, notification);
    }

    private PendingIntent createStopIntent() {
        Intent stopIntent = new Intent(this, DownloadService.class);
        stopIntent.setAction(ACTION_STOP);
        return PendingIntent.getService(this, 0, stopIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private void notifyDownloadComplete(boolean completed) {
        Intent intent = new Intent("com.example.testhttp.DOWNLOAD_STATUS");
        intent.putExtra("completed", completed); // true = completato, false = interrotto
        sendBroadcast(intent);
    }


}
