package com.example.appvideo;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.EditText;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

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

public class LoginActivity extends AppCompatActivity {

    private long sessionId = 0;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_login);

        SharedPreferences prefs = getSharedPreferences("AppVideo", MODE_PRIVATE);
        sessionId = prefs.getLong("session_id", 0);
        //Log.d("app", "session id: "+sessionId);

        if(sessionId == 0){
            sendApi(); //Client is not authenticated
        } else {
            checkState(); //Client is authenticated, check server state
        }
    }

    public void sendApi() {
        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();

        JSONObject json = new JSONObject();
        try {
            json.put("start_auth", true);
            json.put("session_id", String.valueOf(sessionId)); //If the server is shut down, the client needs to tell it to reopen the connection to Telegram servers.
                                                                     //If the client sends the same session_id he used last time, the server will reopen connection without asking again for authentication.
        } catch (JSONException e) {
            e.printStackTrace();
            return;
        }

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json")
        );
        //Log.d("app",getString(R.string.server) + "/auth");

        Request request = new Request.Builder()
                .url(getString(R.string.server) + "/auth")
                .post(body)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                runOnUiThread(() -> {
                    Log.e("app", e.getMessage() + "\n" + e.getCause());
                });
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                String responseData = response.body().string();

                try {
                    JSONObject data = new JSONObject(responseData);
                    int newSessionId = data.getInt("session_id");
                    long unsignedSessionId = newSessionId & 0xffffffffL; //session_id is uint32_t but Java doesn't support unsigned types

                    SharedPreferences prefs = getSharedPreferences("AppVideo", MODE_PRIVATE);
                    SharedPreferences.Editor editor = prefs.edit();
                    editor.putLong("session_id", unsignedSessionId); //Save new session_id
                    editor.apply();

                    //Log.d("app", "nuovo id " + unsignedSessionId);

                    sessionId = unsignedSessionId;

                    checkState();

                } catch (JSONException e) {
                    runOnUiThread(() -> {
                        Log.e("app", e.getMessage() + "\n" + e.getCause());
                    });
                }
            }
        });
    }

    private void checkState() {
        //Log.d("app", "Checking state...");

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();
        String url = getString(R.string.server) + "/auth/get_state?session_id=" + sessionId;

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
                    JSONObject state = new JSONObject(json);
                    String authState = state.getString("state");
                    //Log.d("app", "Current state: " + authState);

                    runOnUiThread(() -> {
                        switch (authState) {
                            case "authorizationStateClosed": //The Telegram session has been closed. A new one needs to be created
                                sessionId = 0;
                                sendApi();
                                break;
                            case "authorizationStateWaitTdlibParameters": //Client not authenticated or server connection to Telegram still not open
                                //Log.d("app", "Waiting for TDLib parameters...");
                                sendApi();
                                break;
                            case "authorizationStateWaitPhoneNumber":
                                showStep("phone");
                                break;
                            case "authorizationStateWaitPassword":
                                showStep("password");
                                break;
                            case "authorizationStateWaitCode":
                                showStep("code");
                                break;
                            case "authorizationStateReady":
                                //Log.d("app", "Logged in");
                                showStep("hide");
                                //Log.d("app", "Id sessione in ready " + sessionId);
                                startActivity(new Intent(LoginActivity.this, MainActivity.class)); //Start MainActivity
                                finish();
                                break;
                            default:
                                Log.w("app", "Unknown state: " + authState);
                                checkState(); // recursive retry
                        }
                    });
                } catch (JSONException e) {
                    Log.e("app", "Failed to parse JSON", e);
                }
            }
        });
    }

    private void showStep(String s) {
        switch (s) {
            case "password":
                findViewById(R.id.phone_number).setVisibility(View.GONE);
                findViewById(R.id.auth_code).setVisibility(View.GONE);
                findViewById(R.id.password).setVisibility(View.VISIBLE);
                findViewById(R.id.phone_number_btn).setVisibility(View.GONE);
                findViewById(R.id.auth_code_btn).setVisibility(View.GONE);
                findViewById(R.id.password_btn).setVisibility(View.VISIBLE);
                break;
            case "code":
                findViewById(R.id.phone_number).setVisibility(View.GONE);
                findViewById(R.id.auth_code).setVisibility(View.VISIBLE);
                findViewById(R.id.password).setVisibility(View.GONE);
                findViewById(R.id.phone_number_btn).setVisibility(View.GONE);
                findViewById(R.id.auth_code_btn).setVisibility(View.VISIBLE);
                findViewById(R.id.password_btn).setVisibility(View.GONE);
                break;
            case "phone":
                findViewById(R.id.phone_number).setVisibility(View.VISIBLE);
                findViewById(R.id.auth_code).setVisibility(View.GONE);
                findViewById(R.id.password).setVisibility(View.GONE);
                findViewById(R.id.phone_number_btn).setVisibility(View.VISIBLE);
                findViewById(R.id.auth_code_btn).setVisibility(View.GONE);
                findViewById(R.id.password_btn).setVisibility(View.GONE);
                break;
            case "hide":
                findViewById(R.id.phone_number).setVisibility(View.GONE);
                findViewById(R.id.auth_code).setVisibility(View.GONE);
                findViewById(R.id.password).setVisibility(View.GONE);
                findViewById(R.id.phone_number_btn).setVisibility(View.GONE);
                findViewById(R.id.auth_code_btn).setVisibility(View.GONE);
                findViewById(R.id.password_btn).setVisibility(View.GONE);
                break;
        }
    }

    public void sendPhone(View view) {
        String phone = ((EditText) findViewById(R.id.phone_number)).getText().toString();

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();

        JSONObject json = new JSONObject();
        try {
            json.put("phone_number", phone);
            json.put("session_id", String.valueOf(sessionId));
        } catch (JSONException e) {
            e.printStackTrace();
            return;
        }

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json")
        );

        //Log.d("app", getString(R.string.server) + "/auth");

        Request request = new Request.Builder()
                .url(getString(R.string.server) + "/auth")
                .post(body)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                runOnUiThread(() -> {
                    Log.e("app", e.getMessage() + "\n" + e.getCause());
                });
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                checkState();
            }
        });
    }

    public void sendCode(View view) {
        String code = ((EditText) findViewById(R.id.auth_code)).getText().toString();

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();

        JSONObject json = new JSONObject();
        try {
            json.put("code", code);
            json.put("session_id", String.valueOf(sessionId));
        } catch (JSONException e) {
            e.printStackTrace();
            return;
        }

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json")
        );

        //Log.d("app", getString(R.string.server) + "/auth");

        Request request = new Request.Builder()
                .url(getString(R.string.server) + "/auth")
                .post(body)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                runOnUiThread(() -> {
                    Log.e("app", e.getMessage() + "\n" + e.getCause());
                });
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                checkState();
            }
        });
    }

    public void sendPassword(View view) {
        String password = ((EditText) findViewById(R.id.password)).getText().toString();

        OkHttpClient client = new OkHttpClient.Builder()
                .hostnameVerifier((hostname, session) -> true)
                .build();

        JSONObject json = new JSONObject();
        try {
            json.put("password", password);
            json.put("session_id", String.valueOf(sessionId));
        } catch (JSONException e) {
            e.printStackTrace();
            return;
        }

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json")
        );

        //Log.d("app", getString(R.string.server) + "/auth");

        Request request = new Request.Builder()
                .url(getString(R.string.server) + "/auth")
                .post(body)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                runOnUiThread(() -> {
                    Log.e("app", e.getMessage() + "\n" + e.getCause());
                });
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    Log.e("app", "Unexpected response: " + response);
                    return;
                }

                checkState();
            }
        });
    }
}