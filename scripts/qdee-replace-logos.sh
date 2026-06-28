#!/usr/bin/env bash
# ============================================================================
# QDEE S2 — Logo replacement helper (Sunshine fork)
# ============================================================================
# Zamienia binarne assety logo Sunshine na logo QDEE.
#
# Wymagania:
#   - Inkscape (SVG -> PNG): https://inkscape.org/
#   - ImageMagick (PNG -> ICO): https://imagemagick.org/
#   - Źródło SVG: <QDEE_repo>/assets/logo/qdee-logo-A.svg
#
# Użycie:
#   QDEE_REPO=/path/to/grejem-QuantumDisplayEngineExtension \
#   SUNSHINE_SRC=/path/to/third-party/Sunshine \
#   bash replace-logos.sh
#
# Po uruchomieniu skrypt zbuforuje wygenerowane PNG/ICO w /tmp/qdee-logos/
# i nadpisze pliki w $SUNSHINE_SRC. Commit na forku po weryfikacji wizualnej.
# ============================================================================
set -euo pipefail

QDEE_REPO="${QDEE_REPO:?Podaj QDEE_REPO}"
SUNSHINE_SRC="${SUNSHINE_SRC:?Podaj SUNSHINE_SRC}"
SRC_SVG="$QDEE_REPO/assets/logo/qdee-logo-A.svg"

if [[ ! -f "$SRC_SVG" ]]; then
  echo "BŁĄD: nie znaleziono $SRC_SVG" >&2
  exit 1
fi
if ! command -v inkscape >/dev/null; then
  echo "BŁĄD: inkscape nie jest w PATH. Zainstaluj: https://inkscape.org/" >&2
  exit 1
fi
if ! command -v magick >/dev/null && ! command -v convert >/dev/null; then
  echo "BŁĄD: ImageMagick (magick/convert) nie jest w PATH." >&2
  exit 1
fi

CACHEDIR="/tmp/qdee-logos"
mkdir -p "$CACHEDIR"

echo "=== Generowanie PNG z SVG ==="
for size in 16 32 45 64 128 256 512; do
  out="$CACHEDIR/qdee-${size}.png"
  inkscape -w "$size" -h "$size" "$SRC_SVG" -o "$out" 2>/dev/null
  echo "  $out"
done

echo "=== Generowanie ICO (multi-size) ==="
MAGICK=$(command -v magick || command -v convert)
"$MAGICK" \
  "$CACHEDIR/qdee-16.png" \
  "$CACHEDIR/qdee-32.png" \
  "$CACHEDIR/qdee-64.png" \
  "$CACHEDIR/qdee-128.png" \
  "$CACHEDIR/qdee-256.png" \
  "$CACHEDIR/qdee.ico"
echo "  $CACHEDIR/qdee.ico"

echo "=== Kopiowanie assetów do Sunshine ==="

# Root icons
cp "$CACHEDIR/qdee-256.png" "$SUNSHINE_SRC/sunshine.png"
cp "$SRC_SVG" "$SUNSHINE_SRC/sunshine.svg"
cp "$CACHEDIR/qdee.ico" "$SUNSHINE_SRC/sunshine.ico"

# Web UI assets
WEB_ASSETS="$SUNSHINE_SRC/src_assets/common/assets/web/public/images"
mkdir -p "$WEB_ASSETS"
cp "$CACHEDIR/qdee-45.png" "$WEB_ASSETS/logo-sunshine-45.png"
cp "$CACHEDIR/qdee-256.png" "$WEB_ASSETS/sunshine.png"
for size in 16 32 64 128 256; do
  cp "$CACHEDIR/qdee-${size}.png" "$WEB_ASSETS/sunshine-${size}.png" 2>/dev/null || true
done

# Windows packaging icons (if present)
WIN_PKG="$SUNSHINE_SRC/packaging/windows"
if [[ -d "$WIN_PKG" ]]; then
  cp "$CACHEDIR/qdee.ico" "$WIN_PKG/sunshine.ico" 2>/dev/null || true
fi

echo ""
echo "=== GOTOWE ==="
echo "Pliki nadpisane w: $SUNSHINE_SRC"
echo "Zweryfikuj wizualnie, potem:"
echo "  cd $SUNSHINE_SRC"
echo "  git add -A"
echo "  git commit -m 'S2: replace Sunshine logos with QDEE branding'"
echo ""
echo "UWAGA: Niektóre rozmiary PNG mogą nie mieć odpowiedników w Sunshine."
echo "Sprawdź 'git status' przed commitem."
