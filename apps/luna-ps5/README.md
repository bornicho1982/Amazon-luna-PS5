# Luna-PS5: Manual de Arquitectura y Despliegue

## 1. Visión General
**Luna-PS5** es un cliente nativo en C++ para PlayStation 5 (arquitectura Prospero `x86_64-sie-ps5`) diseñado para recibir transmisiones de juegos en la nube mediante WebRTC y AWS Kinesis Video Streams a 1080p a 60 FPS con entrada directa del DualSense.

## 2. Estructura del Proyecto
```
apps/luna-ps5/
├── CMakeLists.txt              # Configuración de compilación (Desktop y PS5)
├── build_ps5.ps1               # Compilador nativo LLVM clang++ para PS5
├── deploy.ps1                  # Script de envío directo a la consola (puerto 9021 / FTP)
├── dist/                       # Paquete listo para el menú principal de la consola
│   └── LUNA00001/
│       ├── eboot.bin           # Binario nativo PS5 (1.74 MB)
│       └── sce_sys/
│           ├── icon0.png       # Icono para el menú de la consola
│           └── param.json      # Metadatos del título y categoría
└── src/
    ├── main.cpp                # Bucle a 60 Hz y sincronización de eventos
    ├── auth/                   # Servidor HTTP embebido en puerto 8085 (LAN)
    ├── display/                # Renderizado directo en GPU Garlic (triple buffer 1080p)
    ├── audio/                  # Salida PCM estéreo de baja latencia a 48 kHz
    ├── input/                  # Driver nativo de mando DualSense (scePad)
    ├── media/                  # Decodificador H.264 NAL y sintetizador Opus
    ├── signaling/              # Señalización AWS Kinesis WebRTC (Base64 SDP/ICE)
    └── stream/                 # Pista de medios y canales de datos sin lag
```

## 3. Instrucciones de Despliegue en la Consola

### Método A: Carga como App en el Menú Principal (Recomendado)
1. Conéctate por FTP a tu PS5 (puerto 1337 predeterminado en `ps5lab`).
2. Copia la carpeta completa `apps/luna-ps5/dist/LUNA00001` dentro del directorio `/data/homebrew/` de la consola:
   ```
   /data/homebrew/LUNA00001/
       ├── eboot.bin
       └── sce_sys/
           ├── icon0.png
           └── param.json
   ```
3. El lanzador de homebrew (ShadowMountPlus / etaHEN) registrará la app automáticamente y aparecerá en el carrusel de juegos de la PS5 con el título **Amazon Luna**.

### Método B: Envío rápido como Payload ELF
Si estás probando o depurando directamente en la consola con el cargador de payloads activo (puerto 9021):
```powershell
powershell -ExecutionPolicy Bypass -File apps/luna-ps5/deploy.ps1
```
