# KasaPro V3.5 — Sprint 08

## Delivered
- Variant-specific photo manager directly in the existing variant dialog.
- Preview, replace, or clear product image for selected variant only.
- Import chosen image to application data folder with unique filename, limited file size (15MB) and dimensions (12000px).
- Existing parent-product image remains unchanged. Existing child photos can be overridden individually.
- Failed database write cleans up newly copied file. User originals are never deleted.
- No SQLite schema changes needed (`products.image_path` existed).

## 43-role virtual team review
- Scrum (2): scope/acceptance criteria.
- Architecture (12): avoid changing product IDs and sales foreign keys.
- Backend (4): parameter-bound product image update.
- Frontend (4): selected-variant editor dialog.
- Mobile (7): touch usability review only; mobile app not built.
- UI/UX (4): image preview and dedicated actions.
- QA (4): source checks; no physical Windows execution.
- DevOps (4): existing GitHub Actions Windows build preserved.
- Business analysis (2): retail size/color/model variation.

## Not delivered
- Multi-image galleries, Code 128/EAN13 printed labels, decimal unit end-to-end accounting, physical printer tests.
- MSVC build not executed locally: Qt6 compiler unavailable here.
