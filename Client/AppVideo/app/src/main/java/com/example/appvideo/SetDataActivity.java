package com.example.appvideo;

import android.annotation.SuppressLint;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.MultipartBody;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class SetDataActivity extends AppCompatActivity {
    private static final int PICK_IMAGE_REQUEST = 1;

    private EditText titleEditText, descriptionEditText;
    private Button selectImageButton, submitButton;
    private ImageView selectedImageView;
    private ProgressBar progressBar;
    private TextView statusTextView;

    private long chatId;
    private long messageId;
    private byte[] imageBytes;
    private String imageFileName;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_set_data);

        Intent intent = getIntent();
        chatId = intent.getLongExtra("chat_id", 0);
        messageId = intent.getLongExtra("message_id", 0);

        if (chatId == 0 || messageId == 0) {
            Toast.makeText(this, "Dati mancanti", Toast.LENGTH_SHORT).show();
            finish();
            return;
        }

        titleEditText = findViewById(R.id.titleEditText);
        descriptionEditText = findViewById(R.id.descriptionEditText);
        selectImageButton = findViewById(R.id.selectImageButton);
        selectedImageView = findViewById(R.id.selectedImageView);
        submitButton = findViewById(R.id.submitButton);
        progressBar = findViewById(R.id.progressBar);
        statusTextView = findViewById(R.id.statusTextView);

        selectImageButton.setOnClickListener(v -> openImageChooser());
        submitButton.setOnClickListener(v -> submitVideoData());
    }

    private void openImageChooser() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("image/*");
        startActivityForResult(intent, PICK_IMAGE_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == PICK_IMAGE_REQUEST && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri imageUri = data.getData();
            imageFileName = getFileName(imageUri);

            try {
                InputStream inputStream = getContentResolver().openInputStream(imageUri);
                if (inputStream != null) {
                    ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
                    byte[] buffer = new byte[1024];
                    int bytesRead;
                    while ((bytesRead = inputStream.read(buffer)) != -1) {
                        byteArrayOutputStream.write(buffer, 0, bytesRead);
                    }
                    imageBytes = byteArrayOutputStream.toByteArray();
                    inputStream.close();

                    //Show image preview
                    selectedImageView.setImageURI(imageUri);
                    selectedImageView.setVisibility(View.VISIBLE);
                }
            } catch (IOException e) {
                Log.e("VideoDataActivity", "Error reading image", e);
                Toast.makeText(this, "Errore durante la lettura dell'immagine", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void submitVideoData() {
        String title = titleEditText.getText().toString().trim();
        String description = descriptionEditText.getText().toString().trim();

        if (title.isEmpty()) {
            titleEditText.setError("Inserisci un titolo");
            return;
        }

        if (imageBytes == null) {
            Toast.makeText(this, "Seleziona un'immagine", Toast.LENGTH_SHORT).show();
            return;
        }

        progressBar.setVisibility(View.VISIBLE);
        statusTextView.setVisibility(View.VISIBLE);
        statusTextView.setText("Invio in corso...");
        submitButton.setEnabled(false);

        OkHttpClient client = new OkHttpClient();

        //Multipart request
        RequestBody requestBody = new MultipartBody.Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart("title", title)
                .addFormDataPart("description", description)
                .addFormDataPart("chat_id", String.valueOf(chatId))
                .addFormDataPart("message_id", String.valueOf(messageId))
                .addFormDataPart("image", imageFileName,
                        RequestBody.create(imageBytes, MediaType.parse("image/*")))
                .build();

        Request request = new Request.Builder()
                .url(getString(R.string.server) + "/set_video_data")
                .post(requestBody)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                runOnUiThread(() -> {
                    progressBar.setVisibility(View.GONE);
                    statusTextView.setText("Errore di connessione");
                    submitButton.setEnabled(true);
                    Toast.makeText(SetDataActivity.this, "Errore durante l'invio", Toast.LENGTH_SHORT).show();
                });
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                runOnUiThread(() -> {
                    progressBar.setVisibility(View.GONE);

                    if (response.isSuccessful()) {
                        statusTextView.setText("Dati salvati con successo");
                        Toast.makeText(SetDataActivity.this, "Dati salvati", Toast.LENGTH_SHORT).show();
                        finish();
                    } else {
                        statusTextView.setText("Errore nel salvataggio");
                        submitButton.setEnabled(true);
                        Toast.makeText(SetDataActivity.this, "Errore: " + response.message(), Toast.LENGTH_SHORT).show();
                    }
                });
            }
        });
    }

    @SuppressLint("Range")
    private String getFileName(Uri uri) {
        String result = null;
        if (uri.getScheme().equals("content")) {
            try (android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    result = cursor.getString(cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME));
                }
            }
        }
        if (result == null) {
            result = uri.getPath();
            int cut = result.lastIndexOf('/');
            if (cut != -1) {
                result = result.substring(cut + 1);
            }
        }
        return result;
    }
}