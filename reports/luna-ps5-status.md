# Diagnóstico de Errores en Consola y Solución Definitiva (`PPSA99825`)

**Fecha:** 21 de Octubre de 2026  
**Consola de prueba:** PlayStation 5 FW 13.60 (`192.168.18.21`)

---

## 1. ¿Por qué falló la subida anterior? (Análisis de las 3 capturas)

Al revisar tus 3 capturas (`PS5Upload`, pantalla de error `CE-105773-3` y menú con candado `FAKE02932`), identificamos las **4 causas exactas**:

1. **Ruta de carpeta anidada incorrecta (`/data/homebrew/luna-ps5/dist/LUNA00001`):**
   - Subiste toda la carpeta de código fuente `luna-ps5` en lugar de la carpeta del título.
   - **ShadowMountPlus** solo escanea el primer nivel `/data/homebrew/<TITLE_ID>`. Al estar enterrada 3 carpetas más adentro (`luna-ps5/dist/...`), ShadowMountPlus jamás pudo verla ni montarla, causando el candado y el error `0x80020008`.

2. **Formato de `titleId` inválido (`LUNA00001` en vez de `PPSAxxxxx`) y `param.json` incompleto:**
   - ShadowMountPlus y el montador de juegos de PS5 requieren un ID con formato **`PPSA` + 5 dígitos** (como `PPSA99810` en PSBox o `PPSA99825` ahora) y los campos obligatorios `conceptId`, `contentId`, `applicationDrmType: "free"`, `gameIntent` (`launchActivity`), `masterVersion` y `contentVersion`.

3. **`eboot.bin` era un ELF de payload sin convertir a FSELF de Aplicación (`CE-105773-3`):**
   - El `prospero-lld` básico del SDK produce un ELF tipo `0x0003` (`ET_DYN` con nombres de función en texto plano) pensado para inyectarse por el puerto `9021` (`elfldr`), **no** para ejecutarse desde el menú principal de la PS5.
   - Cuando lanzas un juego desde el menú de la PS5, el kernel exige:
     - Un ejecutable convertido a tipo **`0xFE10` (`ET_SCE_EXEC_ASLR`)** con tabla `PT_SCE_PROCPARAM` (`ORBI`), símbolos convertidos a hashes **NID de 11 caracteres** (`#A#B`) y sufijos `.prx` (no `.sprx`).
     - Envuelto y firmado en un contenedor **FSELF (`0x1D3D154F`)**.
     - El módulo compañero **`sce_module/libc.prx`** firmado en FSELF.

4. **Icono `FAKE02932`:**
   - El `icon0.png` anterior era el archivo de ejemplo del SDK que tenía escrito el texto `FAKE02932`.

---

## 2. Solución Implementada y Verificada

Hemos integrado el pipeline completo de conversión nativa (`ps5-native-tool` + `libc-builder`):

1. **Conversión a Ejecutable Nativo PS5 (`0xFE10`) + Firma FSELF (`0x1D3D154F`):**
   - `eboot.bin` (421,609 bytes): Contenedor FSELF firmado con `authority: 0x3100000000000002`, 12 segmentos, `PT_SCE_PROCPARAM` (`ORBI`), 134 símbolos NID y verificación de integridad **`valid`**.
2. **Módulo Compañero `sce_module/libc.prx` Generado y Firmado:**
   - `sce_module/libc.prx` (1,284,418 bytes): Contenedor FSELF firmado y validado con los 2,566 exports de runtime que exige el cargador de aplicaciones de PS5.
3. **Nuevo `param.json` Estándar (`PPSA99825`) e Icono Propio (`icon0.png`):**
   - `titleId`: `PPSA99825`
   - `contentId`: `UP9000-PPSA99825_00-AMAZONLUNAPS5000`
   - `applicationDrmType`: `"free"`
   - `icon0.png`: Nuevo diseño morado de 512x512 con el emblema **LUNA CLOUD GAMING PS5**.
4. **Carpeta Lista en la Raíz del Proyecto (`C:\proyectos antigravity\webkit PS5 v2\PPSA99825`):**
   - Colocada directamente en la raíz para que no haya confusión al subirla.
