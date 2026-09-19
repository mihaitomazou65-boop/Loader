# Loader

Windows ImGui loader + Node auth API for Render.

## Server (local)

```powershell
cd server
copy .env.example .env
# Edit DATABASE_URL and JWT_SECRET
npm install
npm run dev
```

Health: `GET http://localhost:3000/health`

## Client (Windows)

```powershell
cd client
cmake -B build -DAPI_BASE_URL=http://localhost:3000 -DLOADER_BRAND=Loader
cmake --build build --config Release
.\build\Release\Loader.exe
```

Production API:

```powershell
cmake -B build -DAPI_BASE_URL=https://your-service.onrender.com
```

## Render deploy

1. Push repo to GitHub.
2. Render ? New ? Blueprint ? use `server/render.yaml` (or Web Service, root `server`).
3. Create PostgreSQL, link `DATABASE_URL`.
4. Set `JWT_SECRET` (long random string).
5. Build: `npm install`, Start: `node src/index.js`.

Session token path: `%APPDATA%\Loader\session.json`.

## UI

Poppins / dark theme ported from your `famdada` project (`font.h`, `imgui_settings.h`, `custom::Button`).

## Client (Visual Studio — recommended)

Open `Loader.sln` ? **Release | x64** ? Build.

Output: `x64\Release\Loader.exe`

Change API URL / brand in `Loader.vcxproj` ? `PreprocessorDefinitions`:
`LOADER_API_BASE_URL="https://your-app.onrender.com"` and `LOADER_BRAND="YourBrand"`.
