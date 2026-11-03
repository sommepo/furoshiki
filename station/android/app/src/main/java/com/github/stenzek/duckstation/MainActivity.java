package com.github.stenzek.duckstation;

import android.Manifest;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ListView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.appcompat.widget.Toolbar;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.preference.PreferenceManager;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {
    private static final int REQUEST_EXTERNAL_STORAGE_PERMISSIONS = 1;
    private static final int REQUEST_ADD_DIRECTORY_TO_GAME_LIST = 2;
    private static final int REQUEST_IMPORT_BIOS_IMAGE = 3;
    private static final int REQUEST_START_FILE = 4;
    private static final int REQUEST_SETTINGS = 5;
    private static final int REQUEST_EDIT_GAME_DIRECTORIES = 6;
    private static final int REQUEST_CHOOSE_COVER_IMAGE = 7;

    private GameList mGameList;
    private ListView mGameListView;
    private GameListFragment mGameListFragment;
    private GameGridFragment mGameGridFragment;
    private Fragment mEmptyGameListFragment;
    private boolean mHasExternalStoragePermissions = false;
    private boolean mIsShowingGameGrid = false;
    private String mPathForChosenCoverImage = null;

    public GameList getGameList() {
        return mGameList;
    }

    public boolean shouldResumeStateByDefault() {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        if (!prefs.getBoolean("Main/SaveStateOnExit", true))
            return false;

        // don't resume with challenge mode on
        if (Achievement.willChallengeModeBeEnabled(this))
            return false;

        return true;
    }

    private void setLanguage() {
        String language = PreferenceManager.getDefaultSharedPreferences(this).getString("Main/Language", "none");
        if (language == null || language.equals("none")) {
            return;
        }

        String[] parts = language.split("-");
        if (parts.length < 2)
            return;

        Locale locale = new Locale(parts[0], parts[1]);
        Locale.setDefault(locale);

        Resources res = getResources();
        Configuration config = res.getConfiguration();
        config.setLocale(locale);
        res.updateConfiguration(config, res.getDisplayMetrics());
    }

    private void setTheme() {
        // Reader fork: dark unless chosen otherwise, as the PlayStation-style home is.
        String theme = PreferenceManager.getDefaultSharedPreferences(this).getString("Main/Theme", "dark");
        if (theme == null)
            return;

        if (theme.equals("follow_system")) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM);
        } else if (theme.equals("light")) {
            AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_NO);
        } else if (theme.equals("dark")) {
            AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES);
        }
    }

    private void loadSettings() {
        setLanguage();
        setTheme();

        mIsShowingGameGrid = PreferenceManager.getDefaultSharedPreferences(this).getBoolean("Main/GameGridView", false);
    }

    private void switchGameListView() {
        mIsShowingGameGrid = !mIsShowingGameGrid;
        PreferenceManager.getDefaultSharedPreferences(this)
                .edit()
                .putBoolean("Main/GameGridView", mIsShowingGameGrid)
                .commit();

        updateGameListFragment(true);
        invalidateOptionsMenu();
    }

    private void updateGameListFragment(boolean allowEmpty) {
        final Fragment listFragment = (allowEmpty && mGameList.getEntryCount() == 0) ?
                mEmptyGameListFragment :
                (mIsShowingGameGrid ? mGameGridFragment : mGameListFragment);

        getSupportFragmentManager()
                .beginTransaction()
                .setReorderingAllowed(true).
                replace(R.id.content_fragment, listFragment)
                .commitAllowingStateLoss();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        loadSettings();
        setTitle(null);

        super.onCreate(null);

        setContentView(R.layout.activity_main);
        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        findViewById(R.id.fab_resume).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startEmulation(null, shouldResumeStateByDefault());
            }
        });

        // Set up game list view.
        mGameList = new GameList(this);
        mGameList.addRefreshListener(() -> updateGameListFragment(true));
        // Reader fork: the PlayStation-style home lies over the game list (reader/ui/PsHome.kt).
        mGameList.addRefreshListener(() -> {
            mGamesLookedThrough = true;
            if (mHome != null)
                mHome.gamesChanged();
        });
        if (com.github.stenzek.duckstation.reader.ui.PsHome.enabled(this))
            showHome(true);
        mGameListFragment = new GameListFragment(this);
        mGameGridFragment = new GameGridFragment(this);
        mEmptyGameListFragment = new EmptyGameListFragment(this);
        updateGameListFragment(false);

        mHasExternalStoragePermissions = checkForExternalStoragePermissions();
        if (mHasExternalStoragePermissions)
            completeStartup();
    }

    private void completeStartup() {
        if (!AndroidHostInterface.hasInstance() && !AndroidHostInterface.createInstance(this)) {
            Log.i("MainActivity", "Failed to create host interface");
            throw new RuntimeException("Failed to create host interface");
        }

        AndroidHostInterface.getInstance().setContext(this);
        mGameList.refresh(false, false, this);
        UpdateNotes.displayUpdateNotes(this);
        // Reader fork: the first settings this app makes by the device, and the reader's first
        // questions, once, here rather than over a game.
        com.github.stenzek.duckstation.reader.ReaderGlue.keepFirstSettings(this);
        ControllerAutoMapper.mapBuiltInControlsOnce(this);
        com.github.stenzek.duckstation.reader.ReaderGlue.offerStart(this);
    }

    public void startAddGameDirectory() {
        if (!checkForExternalStoragePermissions())
            return;

        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addCategory(Intent.CATEGORY_DEFAULT);
        i.putExtra(Intent.EXTRA_LOCAL_ONLY, true);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        i.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(Intent.createChooser(i, getString(R.string.main_activity_choose_directory)),
                REQUEST_ADD_DIRECTORY_TO_GAME_LIST);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);

        final MenuItem switchViewItem = menu.findItem(R.id.action_switch_view);
        if (switchViewItem != null) {
            switchViewItem.setTitle(mIsShowingGameGrid ? R.string.action_show_game_list : R.string.action_show_game_grid);
            switchViewItem.setIcon(mIsShowingGameGrid ? R.drawable.ic_baseline_view_list_24 : R.drawable.ic_baseline_grid_view_24);
        }

        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_start_bios) {
            startEmulation(null, false);
        } else if (id == R.id.action_start_file) {
            startStartFile();
        } else if (id == R.id.action_edit_game_directories) {
            Intent intent = new Intent(this, GameDirectoriesActivity.class);
            startActivityForResult(intent, REQUEST_EDIT_GAME_DIRECTORIES);
            return true;
        } else if (id == R.id.action_scan_for_new_games) {
            mGameList.refresh(false, false, this);
        } else if (id == R.id.action_rescan_all_games) {
            mGameList.refresh(true, true, this);
        } else if (id == R.id.action_import_bios) {
            importBIOSImage();
        } else if (id == R.id.action_settings) {
            Intent intent = new Intent(this, SettingsActivity.class);
            startActivityForResult(intent, REQUEST_SETTINGS);
            return true;
        } else if (id == R.id.action_controller_settings) {
            Intent intent = new Intent(this, ControllerSettingsActivity.class);
            startActivity(intent);
            return true;
        } else if (id == R.id.action_memory_card_editor) {
            Intent intent = new Intent(this, MemoryCardEditorActivity.class);
            startActivity(intent);
            return true;
        } else if (id == R.id.action_switch_view) {
            switchGameListView();
            return true;
        } else if (id == R.id.action_show_version) {
            showVersion();
            return true;
        } else if (id == R.id.action_playstation_home) {  // Reader fork: the home back
            showHome(true);
            return true;
        } else if (id == R.id.action_reader_settings) {  // Reader fork: the reader's pages
            com.github.stenzek.duckstation.reader.ReaderGlue.openPage(this, "settings");
            return true;
        } else if (id == R.id.action_reader_stats) {
            com.github.stenzek.duckstation.reader.ReaderGlue.openPage(this, "stats");
            return true;
        } else if (id == R.id.action_reader_games) {
            com.github.stenzek.duckstation.reader.ReaderGlue.openPage(this, "games");
            return true;
        } else if (id == R.id.action_reader_dictionaries) {
            com.github.stenzek.duckstation.reader.ReaderGlue.openPage(this, "dictionaries");
            return true;
        } else if (id == R.id.action_reader_anki) {
            com.github.stenzek.duckstation.reader.ReaderGlue.openPage(this, "anki");
            return true;
        } else if (id == R.id.action_github_respository) {
            openGithubRepository();
            return true;
        } else if (id == R.id.action_discord_server) {
            openDiscordServer();
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        switch (requestCode) {
            case REQUEST_ADD_DIRECTORY_TO_GAME_LIST: {
                if (resultCode != RESULT_OK || data.getData() == null)
                    return;

                // Use legacy storage on devices older than Android 11... apparently some of them
                // have broken storage access framework....
                if (!GameDirectoriesActivity.useStorageAccessFramework(this)) {
                    final String path = FileHelper.getFullPathFromTreeUri(data.getData(), this);
                    if (path != null) {
                        GameDirectoriesActivity.addSearchDirectory(this, path, true);
                        mGameList.refresh(false, false, this);
                        return;
                    }
                }

                try {
                    getContentResolver().takePersistableUriPermission(data.getData(),
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (Exception e) {
                    Toast.makeText(this, "Failed to take persistable permission.", Toast.LENGTH_LONG);
                    e.printStackTrace();
                }

                GameDirectoriesActivity.addSearchDirectory(this, data.getDataString(), true);
                mGameList.refresh(false, false, this);
            }
            break;

            case REQUEST_IMPORT_BIOS_IMAGE: {
                if (resultCode != RESULT_OK)
                    return;

                onImportBIOSImageResult(data.getData());
            }
            break;

            case REQUEST_START_FILE: {
                if (resultCode != RESULT_OK || data.getData() == null)
                    return;

                startEmulation(data.getDataString(), shouldResumeStateByDefault());
            }
            break;

            case REQUEST_SETTINGS: {
                loadSettings();
            }
            break;

            case REQUEST_EDIT_GAME_DIRECTORIES: {
                mGameList.refresh(false, false, this);
            }
            break;

            case REQUEST_CHOOSE_COVER_IMAGE: {
                final String gamePath = mPathForChosenCoverImage;
                mPathForChosenCoverImage = null;
                if (resultCode != RESULT_OK)
                    return;

                finishChooseCoverImage(gamePath, data.getData());
            }
            break;
        }
    }

    private boolean checkForExternalStoragePermissions() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE) ==
                PackageManager.PERMISSION_GRANTED &&
                ContextCompat
                        .checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
                        PackageManager.PERMISSION_GRANTED) {
            return true;
        }

        ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE},
                REQUEST_EXTERNAL_STORAGE_PERMISSIONS);
        return false;
    }

    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        // check that all were successful
        for (int i = 0; i < grantResults.length; i++) {
            if (grantResults[i] == PackageManager.PERMISSION_GRANTED) {
                if (!mHasExternalStoragePermissions) {
                    mHasExternalStoragePermissions = true;
                    completeStartup();
                }
            } else {
                Toast.makeText(this,
                        R.string.main_activity_external_storage_permissions_error,
                        Toast.LENGTH_LONG);
                finish();
            }
        }
    }

    public boolean openGameProperties(String path) {
        Intent intent = new Intent(this, GamePropertiesActivity.class);
        intent.putExtra("path", path);
        startActivity(intent);
        return true;
    }

    public void openGamePopupMenu(View anchorToView, GameListEntry entry) {
        androidx.appcompat.widget.PopupMenu menu = new androidx.appcompat.widget.PopupMenu(this, anchorToView, Gravity.RIGHT | Gravity.TOP);
        menu.getMenuInflater().inflate(R.menu.menu_game_list_entry, menu.getMenu());
        menu.setOnMenuItemClickListener(item -> {
            int id = item.getItemId();
            if (id == R.id.game_list_entry_menu_start_game) {
                startEmulation(entry.getPath(), false);
                return true;
            } else if (id == R.id.game_list_entry_menu_resume_game) {
                startEmulation(entry.getPath(), true);
                return true;
            } else if (id == R.id.game_list_entry_menu_properties) {
                openGameProperties(entry.getPath());
                return true;
            } else if (id == R.id.game_list_entry_menu_choose_cover_image) {
                startChooseCoverImage(entry.getPath());
                return true;
            }
            return false;
        });

        // disable resume state when challenge mode is on
        if (Achievement.willChallengeModeBeEnabled(this)) {
            MenuItem item = menu.getMenu().findItem(R.id.game_list_entry_menu_resume_game);
            if (item != null)
                item.setEnabled(false);
        }

        menu.show();
    }

    // Reader fork: the PlayStation-style home (reader/ui/PsHome.kt), over the game list, which
    // does what it asks.
    private com.github.stenzek.duckstation.reader.ui.PsHome mHome;
    private boolean mGamesLookedThrough = false;

    private final com.github.stenzek.duckstation.reader.ui.PsHome.Host mHomeHost =
            new com.github.stenzek.duckstation.reader.ui.PsHome.Host() {
        @Override
        public java.util.List<com.github.stenzek.duckstation.reader.ui.PsHome.Game> games() {
            final java.util.ArrayList<com.github.stenzek.duckstation.reader.ui.PsHome.Game> games = new java.util.ArrayList<>();
            for (int i = 0; i < mGameList.getEntryCount(); i++) {
                final GameListEntry entry = mGameList.getEntry(i);
                final String region = entry.getRegion() != null ? entry.getRegion().name().replace('_', '-') : null;
                games.add(new com.github.stenzek.duckstation.reader.ui.PsHome.Game(entry.getPath(), entry.getCode(),
                        entry.getTitle(), region, entry.getSize(), entry.getCoverPath()));
            }
            return games;
        }

        @Override
        public boolean gamesReady() {
            return mGamesLookedThrough || mGameList.getEntryCount() > 0;
        }

        @Override
        public void perform(String action, String path) {
            switch (action) {
                case "start":
                    startEmulation(path, shouldResumeStateByDefault());
                    break;
                case "file":
                    startStartFile();
                    break;
                case "console":
                    startEmulation(null, false);
                    break;
                case "resume":
                    startEmulation(null, true);
                    break;
                case "settings":
                    startActivityForResult(new Intent(MainActivity.this, SettingsActivity.class), REQUEST_SETTINGS);
                    break;
                case "controllers":
                    startActivity(new Intent(MainActivity.this, ControllerSettingsActivity.class));
                    break;
                case "folders":
                    startActivityForResult(new Intent(MainActivity.this, GameDirectoriesActivity.class), REQUEST_EDIT_GAME_DIRECTORIES);
                    break;
                case "refresh":
                    mGameList.refresh(false, false, MainActivity.this);
                    break;
                case "bios":
                    importBIOSImage();
                    break;
                case "cards":
                    startActivity(new Intent(MainActivity.this, MemoryCardEditorActivity.class));
                    break;
                case "classic":
                    showHome(false);
                    break;
            }
        }
    };

    private void showHome(boolean show) {
        com.github.stenzek.duckstation.reader.ui.PsHome.setEnabled(this, show);
        if (show) {
            if (mHome == null) {
                mHome = new com.github.stenzek.duckstation.reader.ui.PsHome(this, mHomeHost);
                addContentView(mHome, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));
            }
            mHome.setVisibility(View.VISIBLE);
            mHome.gamesChanged();
            mHome.requestFocus();
            if (getSupportActionBar() != null)
                getSupportActionBar().hide();
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        } else {
            if (mHome != null)
                mHome.setVisibility(View.GONE);
            if (getSupportActionBar() != null)
                getSupportActionBar().show();
            getWindow().getDecorView().setSystemUiVisibility(0);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Reader fork: the home keeps the keys of a controller when the app comes back.
        if (mHome != null && mHome.getVisibility() == View.VISIBLE) {
            mHome.requestFocus();
            mHome.invalidate();
        }
    }

    public boolean startEmulation(String bootPath, boolean resumeState) {
        if (!doBIOSCheck())
            return false;

        Intent intent = new Intent(this, EmulationActivity.class);
        intent.putExtra("bootPath", bootPath);
        intent.putExtra("resumeState", resumeState);
        startActivity(intent);
        return true;
    }

    public void startStartFile() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(Intent.createChooser(intent, getString(R.string.main_activity_choose_disc_image)), REQUEST_START_FILE);
    }

    private void startChooseCoverImage(String gamePath) {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("image/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        mPathForChosenCoverImage = gamePath;
        startActivityForResult(Intent.createChooser(intent, getString(R.string.menu_game_list_entry_choose_cover_image)),
                REQUEST_CHOOSE_COVER_IMAGE);
    }

    private void finishChooseCoverImage(String gamePath, Uri uri) {
        final GameListEntry gameListEntry = mGameList.getEntryForPath(gamePath);
        if (gameListEntry == null)
            return;

        final Bitmap bitmap = FileHelper.loadBitmapFromUri(this, uri);
        if (bitmap == null) {
            Toast.makeText(this, "Failed to open/decode image.", Toast.LENGTH_LONG).show();
            return;
        }

        final String coverFileName = String.format("%s/covers/%s.png",
                AndroidHostInterface.getUserDirectory(), gameListEntry.getTitle());
        try {
            final File file = new File(coverFileName);
            final OutputStream outputStream = new FileOutputStream(file);
            final boolean result = bitmap.compress(Bitmap.CompressFormat.PNG, 100, outputStream);
            outputStream.close();;
            if (!result) {
                file.delete();
                throw new Exception("Failed to compress bitmap.");
            }

            gameListEntry.setCoverPath(coverFileName);
            mGameList.fireRefreshListeners();
        } catch (Exception e) {
            e.printStackTrace();
            Toast.makeText(this, "Failed to save image.", Toast.LENGTH_LONG).show();
        }

        bitmap.recycle();
    }

    private boolean doBIOSCheck() {
        if (AndroidHostInterface.getInstance().hasAnyBIOSImages())
            return true;

        new AlertDialog.Builder(this)
                .setTitle(R.string.main_activity_missing_bios_image)
                .setMessage(R.string.main_activity_missing_bios_image_prompt)
                .setPositiveButton(R.string.main_activity_yes, (dialog, button) -> importBIOSImage())
                .setNegativeButton(R.string.main_activity_no, (dialog, button) -> {
                })
                .create()
                .show();

        return false;
    }

    private void importBIOSImage() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(Intent.createChooser(intent, getString(R.string.main_activity_choose_bios_image)), REQUEST_IMPORT_BIOS_IMAGE);
    }

    private void onImportBIOSImageResult(Uri uri) {
        // This should really be 512K but just in case we wanted to support the other BIOSes in the future...
        final int MAX_BIOS_SIZE = 2 * 1024 * 1024;

        InputStream stream = null;
        try {
            stream = getContentResolver().openInputStream(uri);
        } catch (FileNotFoundException e) {
            Toast.makeText(this, R.string.main_activity_failed_to_open_bios_image, Toast.LENGTH_LONG);
            return;
        }

        ByteArrayOutputStream os = new ByteArrayOutputStream();
        try {
            byte[] buffer = new byte[512 * 1024];
            int len;
            while ((len = stream.read(buffer)) > 0) {
                os.write(buffer, 0, len);
                if (os.size() > MAX_BIOS_SIZE) {
                    throw new IOException(getString(R.string.main_activity_bios_image_too_large));
                }
            }
        } catch (IOException e) {
            new AlertDialog.Builder(this)
                    .setMessage(getString(R.string.main_activity_failed_to_read_bios_image_prefix) + e.getMessage())
                    .setPositiveButton(R.string.main_activity_ok, (dialog, button) -> {
                    })
                    .create()
                    .show();
            return;
        }

        String importResult = AndroidHostInterface.getInstance().importBIOSImage(os.toByteArray());
        String message = (importResult == null) ? getString(R.string.main_activity_invalid_error) : ("BIOS '" + importResult + "' imported.");

        new AlertDialog.Builder(this)
                .setMessage(message)
                .setPositiveButton(R.string.main_activity_ok, (dialog, button) -> {
                })
                .create()
                .show();
    }

    private void showVersion() {
        final String message = AndroidHostInterface.getFullScmVersion();
        new AlertDialog.Builder(this)
                .setTitle(R.string.main_activity_show_version_title)
                .setMessage(message)
                .setPositiveButton(R.string.main_activity_ok, (dialog, button) -> {
                })
                .setNeutralButton(R.string.main_activity_copy, (dialog, button) -> {
                    ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                    if (clipboard != null)
                        clipboard.setPrimaryClip(ClipData.newPlainText(getString(R.string.main_activity_show_version_title), message));
                })
                .create()
                .show();
    }

    private void openGithubRepository() {
        final String url = "https://github.com/stenzek/duckstation";
        final Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        startActivity(browserIntent);
    }

    private void openDiscordServer() {
        final String url = "https://discord.gg/Buktv3t";
        final Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        startActivity(browserIntent);
    }
}
