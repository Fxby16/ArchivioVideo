package com.example.appvideo;

import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.UUID;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class UploadActivity extends AppCompatActivity {
    private long sessionId = 0;
    private String chatId;
    private ProgressBar progressBarHorizontal;
    private TextView progressText;
    private Button cancelButton;
    private volatile boolean isUploadCancelled = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_upload);

        progressBarHorizontal = findViewById(R.id.progressBarHorizontal);
        progressText = findViewById(R.id.progressText);
        cancelButton = findViewById(R.id.cancelButton);

        //Cancel button
        cancelButton.setOnClickListener(v -> {
            isUploadCancelled = true;
            finish();
        });

        Intent receivedIntent = getIntent();
        chatId = receivedIntent.getStringExtra("chatId");

        SharedPreferences prefs = getSharedPreferences("AppVideo", MODE_PRIVATE);
        sessionId = prefs.getLong("session_id", 0);

        //Ask the user to pick a video from memory
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("video/*");
        startActivityForResult(intent, 1);
    }

    public void uploadFileInChunks(File file) {
        final int CHUNK_SIZE = 1024 * 1024; // 1 MB
        long totalSize = file.length();
        String filename = generateRandomFileName(file); //Required to avoid conflicts.
                                                        //It must stay the same for all chunks sent.

        runOnUiThread(() -> {
            progressBarHorizontal.setVisibility(View.VISIBLE);
            progressText.setVisibility(View.VISIBLE);
            cancelButton.setVisibility(View.VISIBLE);
            progressBarHorizontal.setMax(100);
        });

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();

        try (FileInputStream fis = new FileInputStream(file)) {
            byte[] buffer = new byte[CHUNK_SIZE];
            int bytesRead;
            long offset = 0;

            //Read and send 1 MB chunks to the server
            while ((bytesRead = fis.read(buffer)) != -1 && !isUploadCancelled) {
                byte[] chunk = Arrays.copyOf(buffer, bytesRead);

                RequestBody body = RequestBody.create(chunk, MediaType.parse("application/octet-stream"));

                Request request = new Request.Builder()
                        .url(getString(R.string.server) + "/upload")
                        .addHeader("Content-Range", "bytes " + offset + "-" + (offset + bytesRead - 1) + "/" + totalSize)
                        .addHeader("session_id", String.valueOf(sessionId))
                        .addHeader("chat_id", chatId)
                        .addHeader("file_name", filename)
                        .addHeader("original_filename", file.getName())
                        .post(body)
                        .build();

                Response response = client.newCall(request).execute();
                if (!response.isSuccessful()) {
                    Log.e("app", "Failed chunk upload: " + response);
                    showError("Upload fallito");
                    return;
                }

                offset += bytesRead;

                //Update progress
                final int progress = (int) ((offset * 100) / totalSize);
                runOnUiThread(() -> {
                    progressBarHorizontal.setProgress(progress);
                    progressText.setText(progress + "%");
                });
            }

            if (!isUploadCancelled) {
                runOnUiThread(() -> {
                    progressText.setText("Upload completato!");
                    new Handler().postDelayed(() -> finish(), 1000);
                });
            }

        } catch (IOException e) {
            Log.e("app", "Error reading or uploading file", e);
            showError("Errore durante l'upload");
        } finally {
            runOnUiThread(() -> {
                cancelButton.setVisibility(View.GONE);
            });
        }
    }

    private void showError(String message) {
        runOnUiThread(() -> {
            progressText.setText(message);
            progressText.setTextColor(Color.RED);
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == 1 && resultCode == RESULT_OK && data != null) {
            Uri videoUri = data.getData();
            Log.d("app", "Video URI: " + videoUri.toString());

            new Thread(() -> {
                try (InputStream inputStream = getContentResolver().openInputStream(videoUri)) {
                    if (inputStream != null) {
                        File tempFile = File.createTempFile("upload_", ".mp4", getCacheDir());
                        try (FileOutputStream outputStream = new FileOutputStream(tempFile)) {
                            byte[] buffer = new byte[1024];
                            int length;
                            while ((length = inputStream.read(buffer)) > 0 && !isUploadCancelled) {
                                outputStream.write(buffer, 0, length);
                            }
                        }
                        if (!isUploadCancelled) {
                            uploadFileInChunks(tempFile);
                        } else {
                            tempFile.delete();
                        }
                    }
                } catch (IOException e) {
                    Log.e("app", "Errore durante la lettura del file", e);
                    showError("Errore durante la lettura del file");
                }
            }).start();
        }
    }

    public static String generateRandomFileName(File file) {
        String extension = "";
        String name = file.getName();
        int lastDot = name.lastIndexOf('.');
        if (lastDot != -1 && lastDot < name.length() - 1) {
            extension = name.substring(lastDot);
        }
        return UUID.randomUUID().toString().replace("-", "") + extension;
    }
}