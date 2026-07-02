// Smoother nativer Musik-Player
// Pure C++, kein Java. UI: Dear ImGui über OpenGL ES.
// Dekodierung: Android NDK MediaExtractor/MediaCodec (nutzt den eingebauten
// System-MP3-Decoder, kein eigener Decoder nötig).
// Wiedergabe: AAudio (moderne, latenzarme Android Audio API).

#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <aaudio/AAudio.h>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"

#define LOG_TAG "player"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------- Track / Playlist ----------
struct Track {
    std::string path;
    std::string title; // Dateiname ohne Endung
};

static std::vector<Track> g_playlist;
static int g_currentIndex = -1;

// ---------- Dekodierter Audio-Puffer ----------
struct DecodedAudio {
    std::vector<int16_t> samples; // interleaved
    int channelCount = 2;
    int sampleRate = 44100;
    int64_t durationUs = 0;
};

static std::mutex g_audioMutex;
static DecodedAudio g_audio;
static std::atomic<size_t> g_playhead{0}; // Sample-Index (pro Kanal-Frame)
static std::atomic<bool> g_playing{false};
static std::atomic<bool> g_loading{false};
static std::atomic<float> g_volume{0.8f};
static std::atomic<bool> g_trackReady{false};
static std::atomic<bool> g_wantSeekTo{false};
static std::atomic<size_t> g_seekTarget{0};
static std::atomic<bool> g_autoAdvance{false};

static AAudioStream* g_stream = nullptr;

// ---------- Dateien scannen ----------
static void scanDirForMp3(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st{};
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            // eine Ebene tief mitscannen (z.B. Unterordner in Music/)
            scanDirForMp3(full);
        } else {
            if (name.size() > 4) {
                std::string ext = name.substr(name.size() - 4);
                for (auto& c : ext) c = tolower(c);
                if (ext == ".mp3") {
                    std::string title = name.substr(0, name.size() - 4);
                    g_playlist.push_back({full, title});
                }
            }
        }
    }
    closedir(d);
}

static void scanLibrary() {
    g_playlist.clear();
    scanDirForMp3("/storage/emulated/0/Music");
    scanDirForMp3("/storage/emulated/0/Download");
    scanDirForMp3("/sdcard/Music");
}

// ---------- MP3 -> PCM Dekodierung (läuft in eigenem Thread) ----------
static void decodeTrack(std::string path) {
    g_loading = true;
    g_trackReady = false;

    AMediaExtractor* extractor = AMediaExtractor_new();
    if (AMediaExtractor_setDataSource(extractor, path.c_str()) != AMEDIA_OK) {
        LOGE("Konnte Datei nicht öffnen: %s", path.c_str());
        AMediaExtractor_delete(extractor);
        g_loading = false;
        return;
    }

    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, 0);
    const char* mime = nullptr;
    AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);

    int32_t sampleRate = 44100, channelCount = 2;
    int64_t durationUs = 0;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);
    AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &durationUs);

    AMediaExtractor_selectTrack(extractor, 0);

    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    AMediaCodec_configure(codec, format, nullptr, nullptr, 0);
    AMediaCodec_start(codec);

    std::vector<int16_t> pcm;
    pcm.reserve(sampleRate * channelCount * 4); // grobe Vorab-Reservierung

    bool inputDone = false;
    bool outputDone = false;

    while (!outputDone) {
        if (!inputDone) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 5000);
            if (inIdx >= 0) {
                size_t bufSize;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, inIdx, &bufSize);
                ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, buf, bufSize);
                if (sampleSize < 0) {
                    AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0,
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                } else {
                    int64_t presTime = AMediaExtractor_getSampleTime(extractor);
                    AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, presTime, 0);
                    AMediaExtractor_advance(extractor);
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
        if (outIdx >= 0) {
            if (info.size > 0) {
                size_t outSize;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                size_t n = info.size / sizeof(int16_t);
                int16_t* samples = (int16_t*)(outBuf + info.offset);
                pcm.insert(pcm.end(), samples, samples + n);
            }
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                outputDone = true;
            }
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* newFormat = AMediaCodec_getOutputFormat(codec);
            AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
            AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);
            AMediaFormat_delete(newFormat);
        }
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    AMediaExtractor_delete(extractor);

    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_audio.samples = std::move(pcm);
        g_audio.channelCount = channelCount;
        g_audio.sampleRate = sampleRate;
        g_audio.durationUs = durationUs;
    }
    g_playhead = 0;
    g_trackReady = true;
    g_loading = false;
    LOGI("Track dekodiert: %s (%zu samples)", path.c_str(), g_audio.samples.size());
}

static void loadAndPlay(int index) {
    if (index < 0 || index >= (int)g_playlist.size()) return;
    g_currentIndex = index;
    g_playing = false;
    std::thread(decodeTrack, g_playlist[index].path).detach();
}

// ---------- AAudio Callback ----------
static aaudio_data_callback_result_t audioCallback(
    AAudioStream* stream, void* userData, void* audioData, int32_t numFrames) {

    int16_t* out = (int16_t*)audioData;
    int channels = 2;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        channels = g_audio.channelCount > 0 ? g_audio.channelCount : 2;

        if (g_wantSeekTo.load()) {
            g_playhead = g_seekTarget.load();
            g_wantSeekTo = false;
        }

        size_t totalFrames = g_audio.samples.size() / channels;
        size_t pos = g_playhead.load();
        float vol = g_volume.load();

        for (int i = 0; i < numFrames; i++) {
            if (!g_playing.load() || pos >= totalFrames || g_audio.samples.empty()) {
                out[i * 2 + 0] = 0;
                out[i * 2 + 1] = 0;
            } else {
                size_t base = pos * channels;
                int16_t l = g_audio.samples[base];
                int16_t r = (channels > 1) ? g_audio.samples[base + 1] : l;
                out[i * 2 + 0] = (int16_t)(l * vol);
                out[i * 2 + 1] = (int16_t)(r * vol);
                pos++;
            }
        }

        if (g_playing.load() && pos >= totalFrames && totalFrames > 0) {
            g_playing = false;
            g_autoAdvance = true; // Hauptthread soll nächsten Track starten
        }
        g_playhead = pos;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void startAudioStream() {
    AAudioStreamBuilder* builder;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, 44100);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, audioCallback, nullptr);
    AAudioStreamBuilder_openStream(builder, &g_stream);
    AAudioStreamBuilder_delete(builder);
    if (g_stream) {
        AAudioStream_requestStart(g_stream);
    }
}

// ---------- Helper ----------
static std::string formatTime(double seconds) {
    if (seconds < 0 || std::isnan(seconds)) seconds = 0;
    int m = (int)seconds / 60;
    int s = (int)seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// ---------- EGL / App State ----------
struct AppState {
    android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0, height = 0;
    bool animating = false;
};

static void initEGL(AppState* st) {
    st->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(st->display, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(st->display, attribs, &config, 1, &numConfigs);

    EGLint format;
    eglGetConfigAttrib(st->display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(st->app->window, 0, 0, format);

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    st->context = eglCreateContext(st->display, config, EGL_NO_CONTEXT, ctxAttribs);
    st->surface = eglCreateWindowSurface(st->display, config, st->app->window, nullptr);
    eglMakeCurrent(st->display, st->surface, st->surface, st->context);

    eglQuerySurface(st->display, st->surface, EGL_WIDTH, &st->width);
    eglQuerySurface(st->display, st->surface, EGL_HEIGHT, &st->height);
}

static void setupImGuiStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 18.0f;
    s.FrameRounding = 14.0f;
    s.GrabRounding = 20.0f;
    s.WindowPadding = ImVec2(22, 22);
    s.ItemSpacing = ImVec2(10, 14);
    s.FramePadding = ImVec2(14, 12);
    s.ScrollbarSize = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    c[ImGuiCol_FrameBg]       = ImVec4(0.14f, 0.15f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered]= ImVec4(0.18f, 0.19f, 0.26f, 1.00f);
    c[ImGuiCol_Button]        = ImVec4(0.65f, 0.35f, 0.95f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.42f, 1.00f, 1.00f);
    c[ImGuiCol_ButtonActive]  = ImVec4(0.55f, 0.28f, 0.85f, 1.00f);
    c[ImGuiCol_SliderGrab]    = ImVec4(0.72f, 0.42f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.80f, 0.50f, 1.00f, 1.00f);
    c[ImGuiCol_Text]          = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
    c[ImGuiCol_TextDisabled]  = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
    c[ImGuiCol_Header]        = ImVec4(0.65f, 0.35f, 0.95f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.35f, 0.95f, 0.55f);
    c[ImGuiCol_HeaderActive]  = ImVec4(0.65f, 0.35f, 0.95f, 0.75f);
}

// Weiche Interpolation für sanfte Übergänge (z.B. Cover-Pulsieren beim Abspielen)
static float g_pulsePhase = 0.0f;

static void drawUI() {
    ImGuiIO& io = ImGui::GetIO();
    g_pulsePhase += io.DeltaTime * (g_playing.load() ? 2.0f : 0.4f);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Player", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextColored(ImVec4(0.72f, 0.42f, 1.0f, 1.0f), "♪ Player");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 6));

    if (g_playlist.empty()) {
        ImGui::Dummy(ImVec2(0, 30));
        ImGui::TextWrapped("Keine MP3-Dateien gefunden. Leg welche in den Ordner "
            "'Music' oder 'Download' auf deinem Gerät und tippe unten auf Neu laden.");
        ImGui::Dummy(ImVec2(0, 20));
        if (ImGui::Button("Bibliothek neu laden", ImVec2(-1, 55))) {
            scanLibrary();
        }
        ImGui::End();
        return;
    }

    // ---- Cover-Platzhalter mit sanftem Puls-Effekt ----
    float pulse = 0.5f + 0.5f * sinf(g_pulsePhase);
    ImVec2 coverSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x * 0.62f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + coverSize.x, p0.y + coverSize.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 colA = IM_COL32(120 + (int)(pulse * 40), 70, 220, 255);
    ImU32 colB = IM_COL32(60, 40, 140, 255);
    dl->AddRectFilledMultiColor(p0, p1, colA, colB, colB, colA);
    dl->AddRect(p0, p1, IM_COL32(255,255,255,40), 18.0f, 0, 2.0f);
    // Musiknote als simples Symbol in der Mitte
    ImVec2 center = ImVec2((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
    dl->AddText(nullptr, coverSize.y * 0.35f, ImVec2(center.x - coverSize.y * 0.12f, center.y - coverSize.y * 0.22f),
        IM_COL32(255,255,255,220), "\xE2\x99\xAA"); // ♪
    ImGui::Dummy(coverSize);

    ImGui::Dummy(ImVec2(0, 14));

    // ---- Titel ----
    Track& t = g_playlist[g_currentIndex >= 0 ? g_currentIndex : 0];
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextWrapped("%s", g_currentIndex >= 0 ? t.title.c_str() : "Kein Titel geladen");
    ImGui::SetWindowFontScale(1.0f);

    if (g_loading.load()) {
        ImGui::TextColored(ImVec4(0.72f,0.42f,1.0f,1.0f), "Lädt...");
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ---- Fortschrittsbalken ----
    double totalSec = 0, curSec = 0;
    int channels = 2, sampleRate = 44100;
    size_t totalFrames = 0, pos = 0;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        channels = g_audio.channelCount > 0 ? g_audio.channelCount : 2;
        sampleRate = g_audio.sampleRate > 0 ? g_audio.sampleRate : 44100;
        totalFrames = g_audio.samples.size() / channels;
    }
    pos = g_playhead.load();
    totalSec = (double)totalFrames / sampleRate;
    curSec = (double)pos / sampleRate;

    float progress = totalSec > 0 ? (float)(curSec / totalSec) : 0.0f;
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderFloat("##progress", &progress, 0.0f, 1.0f, "")) {
        size_t target = (size_t)(progress * totalFrames);
        g_seekTarget = target;
        g_wantSeekTo = true;
    }
    ImGui::PopItemWidth();

    ImGui::TextDisabled("%s", formatTime(curSec).c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
    ImGui::TextDisabled("%s", formatTime(totalSec).c_str());

    ImGui::Dummy(ImVec2(0, 16));

    // ---- Steuerung: Prev / Play-Pause / Next ----
    float btnW = (ImGui::GetContentRegionAvail().x - 20) / 3;
    if (ImGui::Button("<< Zurück", ImVec2(btnW, 60))) {
        int prev = (g_currentIndex - 1 + g_playlist.size()) % g_playlist.size();
        loadAndPlay(prev);
    }
    ImGui::SameLine();
    bool playing = g_playing.load();
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(btnW, 60))) {
        if (g_trackReady.load()) g_playing = !playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Weiter >>", ImVec2(btnW, 60))) {
        int next = (g_currentIndex + 1) % g_playlist.size();
        loadAndPlay(next);
    }

    ImGui::Dummy(ImVec2(0, 14));

    // ---- Lautstärke ----
    ImGui::Text("Lautstärke");
    float vol = g_volume.load();
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderFloat("##volume", &vol, 0.0f, 1.5f, "")) {
        g_volume = vol;
    }
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    // ---- Playlist ----
    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("Bibliothek (%d Titel)", (int)g_playlist.size());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::BeginChild("playlist", ImVec2(0, 260), true);
    for (int i = 0; i < (int)g_playlist.size(); i++) {
        bool selected = (i == g_currentIndex);
        if (ImGui::Selectable(g_playlist[i].title.c_str(), selected, 0, ImVec2(0, 40))) {
            loadAndPlay(i);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

// ---------- Android Lifecycle ----------
static void handleAppCmd(android_app* app, int32_t cmd) {
    AppState* st = (AppState*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                initEGL(st);
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                setupImGuiStyle();
                ImGui_ImplAndroid_Init(app->window);
                ImGui_ImplOpenGL3_Init("#version 300 es");
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2((float)st->width, (float)st->height);
                io.FontGlobalScale = 2.6f;

                scanLibrary();
                if (!g_playlist.empty()) {
                    loadAndPlay(0);
                }
                startAudioStream();

                st->animating = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            st->animating = false;
            if (g_stream) {
                AAudioStream_requestStop(g_stream);
                AAudioStream_close(g_stream);
                g_stream = nullptr;
            }
            break;
    }
}

static int32_t handleInput(android_app* app, AInputEvent* event) {
    return ImGui_ImplAndroid_HandleInputEvent(event);
}

void android_main(android_app* app) {
    AppState st;
    st.app = app;
    app->userData = &st;
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInput;

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollAll(st.animating ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) return;
        }

        // Track zu Ende -> nächsten automatisch starten
        if (g_autoAdvance.load()) {
            g_autoAdvance = false;
            if (!g_playlist.empty()) {
                int next = (g_currentIndex + 1) % g_playlist.size();
                loadAndPlay(next);
            }
        }
        if (g_trackReady.load() && !g_playing.load() && g_loading.load() == false) {
            // Nach dem Laden eines neuen Tracks automatisch abspielen
            static int lastAutoplayIndex = -1;
            if (lastAutoplayIndex != g_currentIndex) {
                lastAutoplayIndex = g_currentIndex;
                g_playing = true;
            }
        }

        if (st.animating && st.display != EGL_NO_DISPLAY) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplAndroid_NewFrame();
            ImGui::NewFrame();

            drawUI();

            ImGui::Render();
            glViewport(0, 0, st.width, st.height);
            glClearColor(0.06f, 0.06f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            eglSwapBuffers(st.display, st.surface);
        }
    }
}
