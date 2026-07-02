# Player – nativer C++ Musik-Player

Reine C++ App (NativeActivity, kein Java-Code), UI mit Dear ImGui über OpenGL ES.
MP3-Dekodierung über Android's eingebauten Media-Codec (NDK), Wiedergabe über AAudio.
Läuft komplett offline, kein Server, kein WebView, sehr leichtgewichtig & smooth.

## Funktionen
- Automatisches Scannen von MP3-Dateien in `Music/` und `Download/`
- Play / Pause / Zurück / Weiter mit sanften Animationen (pulsierendes Cover)
- Fortschrittsbalken zum Vor-/Zurückspulen (antippen & ziehen)
- Lautstärkeregler
- Playlist / Bibliotheksansicht, antippbar
- Automatischer Sprung zum nächsten Titel

## Nach der Installation: Speicherzugriff erlauben
Da die App keinen Java-Code hat, gibt es keinen automatischen Berechtigungs-Dialog.
Nach der Installation einmal manuell erlauben:

**Einstellungen → Apps → Player → Berechtigungen → Musik und Audio / Speicher → Erlauben**

Danach die App neu starten, damit sie die Musikbibliothek einlesen kann.

## Einmaliger Schritt: Dear ImGui herunterladen
(Falls du das Projekt frisch klonst und `imgui/` leer ist)
```bash
cd app/src/main/cpp
git clone --depth 1 https://github.com/ocornut/imgui.git imgui_tmp
cp imgui_tmp/*.cpp imgui_tmp/*.h imgui/
mkdir -p imgui/backends
cp imgui_tmp/backends/imgui_impl_android.* imgui/backends/
cp imgui_tmp/backends/imgui_impl_opengl3*.* imgui/backends/
rm -rf imgui_tmp
```

## Bauen
Am einfachsten über GitHub Actions (Cloud-Build, siehe `.github/workflows/build.yml`) –
push einfach auf `main` und die fertige APK erscheint unter dem Tab "Actions" als
Artefakt zum Download. Alternativ Android Studio oder Termux+Gradle lokal (siehe
Verlauf/Anleitung im Chat).

## Icon
`app/src/main/res/mipmap/ic_launcher.png` (z.B. 512x512 PNG)

## Erweiterungsideen
- Album-Cover aus MP3-Metadaten (ID3-Tags) auslesen statt Platzhalter
- Shuffle / Repeat-Modus
- Ordner-Auswahl statt feste Pfade
- Equalizer
