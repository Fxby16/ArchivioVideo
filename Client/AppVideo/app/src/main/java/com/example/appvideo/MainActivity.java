package com.example.appvideo;

import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.ActionBarDrawerToggle;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;

import com.bumptech.glide.Glide;
import com.google.android.material.navigation.NavigationView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {
    private long sessionId = 0;
    private long currentChatId = 0;
    private DrawerLayout drawerLayout;
    private ActionBarDrawerToggle drawerToggle;
    private ImageButton refreshButton;
    private TextView toolbarTitle;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        SharedPreferences prefs = getSharedPreferences("AppVideo", MODE_PRIVATE);
        sessionId = prefs.getLong("session_id", 0);
        //Log.d("app", "session id " + sessionId);

        if(sessionId == 0){
            startActivity(new Intent(this, LoginActivity.class)); //If not authenticated go back to login
            finish();
            return;
        }

        setContentView(R.layout.activity_main);
        runOnUiThread(() -> {
            ((TextView) findViewById(R.id.output)).setText("Session ID: " + sessionId);
        });

        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        refreshButton = toolbar.findViewById(R.id.refresh_button);
        toolbarTitle = toolbar.findViewById(R.id.toolbar_title);

        //Listener for refresh button
        refreshButton.setOnClickListener(v -> {
            if (currentChatId != 0) {
                getFiles(String.valueOf(currentChatId));
                Toast.makeText(MainActivity.this, "Refreshing...", Toast.LENGTH_SHORT).show();
            }
        });

        if (getSupportActionBar() != null) {
            getSupportActionBar().setDisplayHomeAsUpEnabled(true);
            getSupportActionBar().setHomeButtonEnabled(true);
        }

        drawerLayout = findViewById(R.id.drawer);
        drawerToggle = new ActionBarDrawerToggle(
                this,
                drawerLayout,
                toolbar,
                R.string.drawer_open,
                R.string.drawer_close);

        drawerLayout.addDrawerListener(drawerToggle);
        drawerToggle.syncState();

        getChats(); //Request chat list
    }

    private void getChats() {
        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();
        String url = getString(R.string.server) + "/get_chats?session_id=" + sessionId;

        Request request = new Request.Builder()
                .url(url)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                Log.e("Auth", "Request failed", e);
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                String json = response.body().string();
                try {
                    JSONArray chats = new JSONArray(json);
                    runOnUiThread(() -> {
                        NavigationView navigationView = findViewById(R.id.nav_view);
                        Menu menu = navigationView.getMenu();

                        int chatGroupId = View.generateViewId();
                        menu.add(chatGroupId, Menu.NONE, Menu.NONE, "Le tue chat")
                                .setEnabled(false);

                        for (int i = 0; i < chats.length(); i++) {
                            try {
                                JSONObject chat = chats.getJSONObject(i);
                                final String id = chat.getString("id");
                                final String title = chat.getString("title");

                                MenuItem chatItem = menu.add(chatGroupId, Menu.NONE, Menu.NONE, title);
                                chatItem.setCheckable(true);

                                //Listener for chat click
                                chatItem.setOnMenuItemClickListener(item -> {
                                    findViewById(R.id.upload).setVisibility(View.VISIBLE);

                                    navigationView.setCheckedItem(item.getItemId());

                                    currentChatId = Long.valueOf(id);

                                    toolbarTitle.setText(title);

                                    getFiles(id); //Get files in the selected chat

                                    Button uploadBtn = findViewById(R.id.upload);
                                    uploadBtn.setVisibility(View.VISIBLE);
                                    uploadBtn.setOnClickListener(v -> {
                                        startActivity(new Intent(MainActivity.this, UploadActivity.class) //Upload activity
                                                .putExtra("chatId", id));
                                    });

                                    DrawerLayout drawer = findViewById(R.id.drawer);
                                    drawer.closeDrawer(GravityCompat.START);

                                    return true;
                                });
                            }catch(JSONException e){
                                Log.e("app", e.getMessage());
                            }
                        }
                    });
                } catch (JSONException e) {
                    Log.e("app", "Failed to parse JSON", e);
                }
            }
        });
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (drawerToggle.onOptionsItemSelected(item)) {
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private void getFiles(String chat_id) {
        //Log.d("app", "get files");
        runOnUiThread(() -> {
            refreshButton.setVisibility(View.VISIBLE);
        });

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();
        String url = getString(R.string.server) + "/get_files?session_id=" + sessionId + "&chat_id=" + chat_id;

        Request request = new Request.Builder()
                .url(url)
                .build();

        client.newCall(request).enqueue(new Callback() { //Get Telegram data
            @Override
            public void onFailure(Call call, IOException e) {
                Log.e("Auth", "Request failed", e);
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Failed to fetch files", Toast.LENGTH_SHORT).show());
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                String json = response.body().string();
                try {
                    JSONArray videosArray = new JSONArray(json);
                    //Log.d("app", videosArray.toString(4));

                    try{
                        JSONObject requestBody = new JSONObject();
                        requestBody.put("videos", videosArray);
                        requestBody.put("chat_id", chat_id);

                        RequestBody body2 = RequestBody.create(
                                requestBody.toString(), MediaType.parse("application/json")
                        );

                        Request request2 = new Request.Builder()
                                .url(getString(R.string.server) + "/get_videos_data")
                                .post(body2)
                                .build();

                        OkHttpClient client2 = new OkHttpClient.Builder()
                                .hostnameVerifier((hostname, session) -> true)
                                .build();

                        client2.newCall(request2).enqueue(new Callback() { //Get server's database data (also contains the data we send)
                            @Override
                            public void onFailure(Call call, IOException e) {
                                Log.e("Auth", "Request failed", e);
                                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Failed to fetch video data", Toast.LENGTH_SHORT).show());
                            }

                            @Override
                            public void onResponse(Call call, Response response) throws IOException {
                                String json2 = response.body().string();
                                try {
                                    JSONArray videosData = new JSONArray(json2);
                                    //Log.d("app", "Received JSON: " + videosData.toString(4));
                                    runOnUiThread(() -> {
                                        LinearLayout messagesListContainer = findViewById(R.id.messages);
                                        messagesListContainer.removeAllViews();

                                        DisplayMetrics metrics = new DisplayMetrics();
                                        getWindowManager().getDefaultDisplay().getMetrics(metrics);
                                        int screenWidth = metrics.widthPixels;
                                        int imageHeight = (int) (screenWidth * 9.0 / 16.0);

                                        //Log.d("app", screenWidth + " " + imageHeight);

                                        for (int i = 0; i < videosData.length(); i++) {
                                            try {
                                                JSONObject video = videosData.getJSONObject(i);

                                                String title = "";
                                                String date = "";
                                                String thumbnailId = "";
                                                boolean hasTelegramData = video.has("telegram_data");

                                                if (hasTelegramData) {
                                                    title = video.optString("title", "");
                                                    if (title.isEmpty()) {
                                                        JSONObject telegramData = video.getJSONObject("telegram_data");
                                                        title = telegramData.optString("file_name", "No title");
                                                    }
                                                    date = video.getJSONObject("telegram_data").optString("date", "No date");
                                                    thumbnailId = video.optString("thumbnail_id", "");
                                                } else {
                                                    title = video.optString("file_name", "No title");
                                                    date = video.optString("date", "No date");
                                                }

                                                LinearLayout videoContainer = new LinearLayout(MainActivity.this);
                                                videoContainer.setOrientation(LinearLayout.VERTICAL);
                                                videoContainer.setLayoutParams(new LinearLayout.LayoutParams(
                                                        LinearLayout.LayoutParams.MATCH_PARENT,
                                                        LinearLayout.LayoutParams.WRAP_CONTENT
                                                ));
                                                videoContainer.setPadding(0, 0, 0, dpToPx(16));
                                                videoContainer.setOnClickListener(v -> {
                                                    Intent intent = new Intent(MainActivity.this, VideoDetailsActivity.class); //Open selected video
                                                    intent.putExtra("video", video.toString());
                                                    intent.putExtra("session_id", sessionId);
                                                    startActivity(intent);
                                                });

                                                ImageView thumbnailView = new ImageView(MainActivity.this);
                                                LinearLayout.LayoutParams thumbParams = new LinearLayout.LayoutParams(
                                                        screenWidth,
                                                        imageHeight
                                                );

                                                thumbnailView.setLayoutParams(thumbParams);
                                                thumbnailView.setScaleType(ImageView.ScaleType.FIT_CENTER);
                                                thumbnailView.setAdjustViewBounds(false);

                                                //Request and load thumbnail
                                                if (!thumbnailId.isEmpty()) {
                                                    String imageUrl = getString(R.string.server) + "/image?id=" + thumbnailId;
                                                    Glide.with(MainActivity.this)
                                                            .load(imageUrl)
                                                            .placeholder(R.drawable.placeholder_video)
                                                            .error(R.drawable.placeholder_video)
                                                            .into(thumbnailView);
                                                } else {
                                                    thumbnailView.setImageResource(R.drawable.placeholder_video);
                                                }

                                                LinearLayout textContainer = new LinearLayout(MainActivity.this);
                                                textContainer.setOrientation(LinearLayout.HORIZONTAL);
                                                textContainer.setPadding(dpToPx(16), dpToPx(8), dpToPx(16), 0);

                                                LinearLayout titleDateLayout = new LinearLayout(MainActivity.this);
                                                titleDateLayout.setOrientation(LinearLayout.VERTICAL);
                                                titleDateLayout.setLayoutParams(new LinearLayout.LayoutParams(
                                                        0,
                                                        LinearLayout.LayoutParams.WRAP_CONTENT,
                                                        1
                                                ));

                                                TextView titleView = new TextView(MainActivity.this);
                                                titleView.setText(title);
                                                titleView.setTextSize(16);
                                                titleView.setMaxLines(2);
                                                titleView.setEllipsize(TextUtils.TruncateAt.END);

                                                TextView dateView = new TextView(MainActivity.this);
                                                dateView.setText(date);
                                                dateView.setTextSize(12);
                                                dateView.setPadding(0, dpToPx(4), 0, 0);

                                                ImageButton editButton = new ImageButton(MainActivity.this);
                                                editButton.setImageResource(android.R.drawable.ic_menu_edit);
                                                editButton.setBackground(null);
                                                editButton.setColorFilter(Color.WHITE);
                                                editButton.setOnClickListener(v -> {
                                                    long message_id = 0;
                                                    if(video.has("message_id")){
                                                        message_id = video.optLong("message_id");
                                                    }else{
                                                        try {
                                                            message_id = video.getJSONObject("telegram_data").optLong("message_id");
                                                        } catch (JSONException e) {
                                                            Log.e("app", e.getMessage());
                                                        }
                                                    }
                                                    Intent intent = new Intent(MainActivity.this, SetDataActivity.class);
                                                    intent.putExtra("chat_id", currentChatId);
                                                    intent.putExtra("message_id", message_id);
                                                    startActivity(intent);
                                                });

                                                titleDateLayout.addView(titleView);
                                                titleDateLayout.addView(dateView);
                                                textContainer.addView(titleDateLayout);
                                                textContainer.addView(editButton);

                                                videoContainer.addView(thumbnailView);
                                                videoContainer.addView(textContainer);

                                                messagesListContainer.addView(videoContainer);

                                            } catch(JSONException e) {
                                                Log.e("app", "Error parsing video item", e);
                                            }
                                        }
                                    });
                                } catch (Exception e) {
                                    Log.d("app", e.getMessage());
                                }
                            }
                        });
                    }catch(JSONException e){
                        Log.d("ERROR", e.getMessage());
                    }
                } catch (JSONException e) {
                    Log.e("app", "Failed to create request body", e);
                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Failed to prepare request", Toast.LENGTH_SHORT).show());
                }
            }
        });
    }

    private int dpToPx(int dp) {
        return (int) (dp * getResources().getDisplayMetrics().density);
    }

}
