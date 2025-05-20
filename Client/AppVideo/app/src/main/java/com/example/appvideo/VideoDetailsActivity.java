package com.example.appvideo;

import android.Manifest;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import com.google.android.exoplayer2.ExoPlayer;
import com.google.android.exoplayer2.MediaItem;
import com.google.android.exoplayer2.PlaybackException;
import com.google.android.exoplayer2.source.ProgressiveMediaSource;
import com.google.android.exoplayer2.ui.PlayerView;
import com.google.android.exoplayer2.upstream.DefaultHttpDataSource;

import org.json.JSONObject;

import android.content.res.Configuration;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;

public class VideoDetailsActivity extends AppCompatActivity {

    private PlayerView playerView;
    private ExoPlayer player;
    private String videoUrl;
    private String fileName;

    private boolean isDownloading = false;
    private static final int NOTIFICATION_PERMISSION_REQUEST_CODE = 1001;

    private final BroadcastReceiver downloadReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            //Receive download status from service
            boolean completed = intent.getBooleanExtra("completed", false);
            if (completed) {
                showNotification("Download completato", fileName + " è stato scaricato con successo");
            } else {
                showNotification("Download interrotto", "Il download di " + fileName + " è stato interrotto");
            }

            isDownloading = false;
            ((Button)findViewById(R.id.download_button)).setText("Scarica video");
        }
    };

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_video_details);

        checkAndRequestNotificationPermission();

        playerView = findViewById(R.id.player_view);

        TextView titleView = findViewById(R.id.video_title);
        TextView descView = findViewById(R.id.video_description);
        TextView dateView = findViewById(R.id.video_date);
        Button downloadBtn = findViewById(R.id.download_button);

        downloadBtn.setOnClickListener(v -> toggleDownload());


        String jsonString = getIntent().getStringExtra("video");
        long sessionID = getIntent().getLongExtra("session_id", 0);
        if (jsonString == null || jsonString.isEmpty()) {
            showError("No video data received");
            return;
        }

        try {
            JSONObject video = new JSONObject(jsonString);

            //Log.d("app", video.toString(4));

            //Get parameters from JSON
            if (video.has("telegram_data")) {
                videoUrl = getString(R.string.server) + "/video?file_id=" + video.getJSONObject("telegram_data").optString("id", "") + "&session_id=" + sessionID;
            } else {
                videoUrl = getString(R.string.server) + "/video?file_id=" + video.optString("id", "") + "&session_id=" + sessionID;
            }

            String title = "";
            if (video.has("telegram_data")) {
                title = video.optString("title", "Nessun titolo");
                fileName = video.getJSONObject("telegram_data").optString("file_name");
            } else {
                title = video.optString("file_name", "Nessun titolo");
                fileName = title;
            }
            titleView.setText(title);

            String description = "";
            if (video.has("telegram_data")) {
                description = video.optString("description", "Nessuna descrizione");
            } else {
                description = video.optString("message_text", "Nessuna descrizione");
                if(description.isEmpty()){
                    description = "Nessuna descrizione";
                }
            }
            descView.setText(description);

            String date = "";
            if (video.has("telegram_data")) {
                date = video.getJSONObject("telegram_data").optString("date", "Nessuna data");
            } else {
                date = video.optString("date", "Nessuna descrizione");
            }
            dateView.setText("Caricato il: " + date);

            initializePlayer();

            adjustLayout(getResources().getConfiguration().orientation);
        } catch (Exception e) {
            e.printStackTrace();
            showError("Invalid video data" + e.getMessage());
        }
    }

    private void initializePlayer() {
        DefaultHttpDataSource.Factory dataSourceFactory = new DefaultHttpDataSource.Factory()
                .setConnectTimeoutMs(10_000)  // 10 secondi connect timeout
                .setReadTimeoutMs(15_000);    // 15 secondi read timeout

        player = new ExoPlayer.Builder(this)
                .setMediaSourceFactory(
                        new ProgressiveMediaSource.Factory(dataSourceFactory))
                .build();

        playerView.setPlayer(player);

        //Log.d("app", videoUrl);
        MediaItem mediaItem = MediaItem.fromUri(Uri.parse(videoUrl));
        player.setMediaItem(mediaItem);

        player.prepare();
        player.play();

        //Error listener
        player.addListener(new com.google.android.exoplayer2.Player.Listener() {
            @Override
            public void onPlayerError(PlaybackException error) {
                Log.e("VideoError", "Playback error: " + error.getMessage());
                if (error.errorCode == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_FAILED) {
                    Log.e("VideoError", "Network connection failed");
                } else if (error.errorCode == PlaybackException.ERROR_CODE_IO_FILE_NOT_FOUND) {
                    Log.e("VideoError", "File not found");
                }

                String stackTrace = Log.getStackTraceString(error);
                showError("Playback error: " + error.getMessage() + " " + error.getCause() + " " + stackTrace);
            }
        });
    }

    private void showError(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
        Log.e("app", message);
        finish();
    }

    @Override
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter("com.example.testhttp.DOWNLOAD_STATUS");
        registerReceiver(downloadReceiver, filter);
    }

    @Override
    protected void onStop() {
        super.onStop();
        unregisterReceiver(downloadReceiver);
        if (player != null) {
            player.release();
            player = null;
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        adjustLayout(newConfig.orientation);
    }


    private void adjustLayout(int orientation) {
        View infoContainer = findViewById(R.id.video_info_container);

        View downloadBtn = findViewById(R.id.download_button);
        downloadBtn.setVisibility(orientation == Configuration.ORIENTATION_PORTRAIT ? View.VISIBLE : View.GONE);

        if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
            getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
            infoContainer.setVisibility(View.GONE);
            playerView.getLayoutParams().height = ViewGroup.LayoutParams.MATCH_PARENT;
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            infoContainer.setVisibility(View.VISIBLE);

            int screenWidth = getResources().getDisplayMetrics().widthPixels;
            int videoHeight = (int) (screenWidth / (16f / 9f));
            playerView.getLayoutParams().height = videoHeight;
        }

        playerView.requestLayout();
    }

    private void toggleDownload() {
        Intent intent = new Intent(this, DownloadService.class);
        if (!isDownloading) {
            //Start download service
            intent.setAction(DownloadService.ACTION_START);
            intent.putExtra("videoUrl", videoUrl);
            intent.putExtra("fileName", fileName);
            startService(intent);
            isDownloading = true;
            ((Button)findViewById(R.id.download_button)).setText("Ferma download");
        } else {
            //Stop download service
            intent.setAction(DownloadService.ACTION_STOP);
            startService(intent);
            isDownloading = false;
            ((Button)findViewById(R.id.download_button)).setText("Scarica video");
        }

    }

    private void checkAndRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {

                // Spiega all'utente perché hai bisogno del permesso (opzionale)
                if (ActivityCompat.shouldShowRequestPermissionRationale(this,
                        Manifest.permission.POST_NOTIFICATIONS)) {
                    // Mostra una spiegazione all'utente
                    new AlertDialog.Builder(this)
                            .setTitle("Permesso notifiche richiesto")
                            .setMessage("Per ricevere gli aggiornamenti sui download, abbiamo bisogno del permesso per le notifiche")
                            .setPositiveButton("OK", (dialog, which) -> {
                                // Richiedi il permesso dopo la spiegazione
                                requestNotificationPermission();
                            })
                            .setNegativeButton("Annulla", null)
                            .create()
                            .show();
                } else {
                    // Richiedi direttamente il permesso
                    requestNotificationPermission();
                }
            }
        }
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    NOTIFICATION_PERMISSION_REQUEST_CODE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (requestCode == NOTIFICATION_PERMISSION_REQUEST_CODE) {
            if (grantResults.length > 0 &&
                    grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                // Permesso concesso
                Toast.makeText(this, "Permesso notifiche concesso", Toast.LENGTH_SHORT).show();
            } else {
                // Permesso negato
                Toast.makeText(this, "Permesso notifiche negato", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void showNotification(String title, String message) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            return; // Non mostrare la notifica se il permesso non è concesso
        }

        NotificationManager notificationManager =
                (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    "download_channel",
                    "Download Notifications",
                    NotificationManager.IMPORTANCE_DEFAULT);
            notificationManager.createNotificationChannel(channel);
        }

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, "download_channel")
                .setSmallIcon(R.drawable.ic_notification)
                .setContentTitle(title)
                .setContentText(message)
                .setPriority(NotificationCompat.PRIORITY_DEFAULT);

        notificationManager.notify(1, builder.build());
    }

}
