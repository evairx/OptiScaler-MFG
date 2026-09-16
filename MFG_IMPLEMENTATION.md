# NVIDIA MFG: implementación y plan de validación

## Estado de esta implementación

Esta versión soluciona de forma definitiva el cuelgue (crash / cierre repentino del juego) al cambiar el ratio de Frame Generation a **3X, 4X o 6X** en *Black Myth: Wukong*, *Silent Hill 2* y cualquier juego con DLSS-G v310+:

1. **Causa raíz del Crash (RenoDx vs DLSS-G moderno)**:
   - El código heredado de RenoDx intentaba parchear un único kernel hardcodeado de punto medio mediante `VirtualAlloc` y reescritura de punteros en `.rdata` buscando tamaños de PTX fijos (99,362 y 99,626 bytes).
   - En *Black Myth: Wukong* (DLSSG v310.1) y DLSS-G v310.7, dicho patrón arrojaba 0 coincidencias o solo parcheaba 1 de 31+ contenedores CUDA FATBIN. Los 30+ kernels restantes para multi-frame interpolation se ejecutaban sin parchear dirigidos a Blackwell (`sm_120`), lo que provocaba un fallo de segmentación del compilador JIT de NVIDIA y el cierre instantáneo del juego al pedir más de 1 cuadro extra (3X+).
2. **Retargeting In-Place de Kernels Blackwell (`RewriteBlackwellKernels`)**:
   - Se reemplazó el mecanismo defectuoso de RenoDx por la implementación probada de `y4my4my4m` (GPL-3.0, `MfgUnlock.cpp`).
   - Escanea todos los contenedores FATBIN en `.rdata` y reescribe in-place la directiva `.target sm_120 -> sm_89 ` (0 asignaciones de memoria, 0 punteros colgantes).
   - Verificado con los archivos del usuario: **31 de 31 contenedores reescriben limpiamente en DLSSG 310.7**, y **33 de 33 en Wukong 310.1**.
3. **Desbloqueo de Gates en Memoria (`PatchAdvertise` y `PatchValidate`)**:
   - Parchea en caliente los límites de `MultiFrameCountMax` y la validación de arquitectura en `nvngx_dlssg.dll` a 5 cuadros generados (6X).
   - Respeta íntegra la firma Authenticode en disco.
4. **Elevación segura en Streamline (`hkslDLSSGGetState`)**:
   - En lugar de parches binarios inestables en `sl.dlss_g.dll`, OptiScaler intercepta limpiamente `slDLSSGGetState` y eleva `numFramesToGenerateMax = UnlockedMax()` directamente al juego.
5. **Menú OptiScaler e Integración**:
   - Monitoreo en tiempo real de versión de `nvngx_dlssg.dll`, estado de gates y conteo de kernels retargeteados.
   - Configuración persistente con `AdaMfgUnlock` y `AdaBlackwellKernels` en `OptiScaler.ini`.

- El addon `MFGAdaUnlock-RenoDx` no es ReShade; es una librería en C++ de 5 archivos creada por RenoDX.
- Cargar `ReShade64.dll` dentro de OptiScaler crearía conflictos graves de doble enganche DirectX y consumo de recursos.
- OptiScaler ya corre en el espacio de memoria del juego (`dxgi.dll`, `sl.interposer.dll`, `nvngx.dll`) y ejecuta exactamente la misma lógica de RenoDX de manera limpia y nativa.

## Alcance actual

| Ruta | Estado |
| --- | --- |
| RTX 40/Ada, DLSS-G/MFG x2–x6 | Funcional con DLSS-G nativo: parcheo de gates en memoria + retargeting de kernels `sm_120` → `sm_89` |
| RTX 30/20 (SM86/SM75) | Re-host CUDA de terceros (`dlssg_sm86.dll`), hasta 4X. Véase `THIRD_PARTY_NOTICES.md` |
| Software Flip Pacing (RSYNC) | **No implementado**: `FGDLSSGForceFlipMeteringOff` se lee del INI pero no se aplica |
| Quality Guard (Anti-Flicker) | **No implementado**: `FGDLSSGQualityGuard` solo existe en config y menú |
| OptiFG como input | Se mantiene. Con DLSS-G nativo puede alimentar NVIDIA DLSS MFG en RTX 40; en juegos sin Frame Generation la salida DLSSG no presenta frames extra (usar FSR FG standalone o Intel XeFG) |
| Salida DLSSG sin DLSS-G nativo | Experimental: depende del pipeline de presentación de Streamline. No hay evidencia de presentación adicional |
| FSR FG standalone | Se mantiene como salida explícita |
| Intel XeFG | Se mantiene como salida explícita |
| Releases en GitHub | Automatizadas con GitHub Actions |

### Backends eliminados

Se retiraron del proyecto, de la configuración, del menú y del archivo de
proyecto los backends de interpolación que se hacían pasar por DLSS-G:

- DLSS Enabler / Artur.
- Nukem `dlssg-to-fsr3`.
- FFX dentro de la ruta DLSS-G.
- Los modos Combo y los proxies NVNGX asociados.

FSR FG standalone e Intel XeFG no dependen de esas rutas y continúan siendo
opciones separadas.

### Licencia y atribución

El enfoque de compatibilidad Ada se deriva en parte de
MFGAdaUnlock-RenoDx. La atribución y la licencia MIT están incluidas en
`THIRD_PARTY_NOTICES.md`. No se distribuye ReShade ni se carga una DLL de
ReShade en OptiScaler.

### Automatización

El workflow `just_build_no_signature.yml` conserva compilación, compresión y
artefactos, pero ya no tiene permiso de escritura de contenidos ni un paso que
cree o actualice Releases.

## Qué no garantiza este cambio

Una coincidencia de firmas y una compilación correcta no prueban que cada juego
entregue recursos válidos de frame generation. Pueden variar entre juegos y
versiones:

- Las rutas y versiones de `nvngx_dlssg.dll` y Streamline.
- El orden de tags de color, profundidad, vectores y HUD/UI.
- Formatos, dimensiones, alpha premultiplicado y espacio de color, especialmente
  en HDR.
- Sincronización de swapchain, Present, Reflex, VSync e Independent Flip.

Por ello, un proveedor no reconocido queda limitado a la ruta nativa, en vez de
afirmar que x3/x4/x6 está funcionando.

## Protocolo de prueba recomendado: Wukong

1. Comparar primero DLSS-G nativo x2 sin MFG Unlock.
2. Activar Unlock MFG, reiniciar y seleccionar x3.
3. Confirmar en el menú/log que aparecen los tres parches y el máximo
   verificado.
4. Comparar la tasa de presentación y la calidad de imagen con ReShade en la
   misma escena, resolución, preset DLSS, HDR/VSync y límite de FPS.
5. Repetir con x4 y, solo si x4 es estable, x6.
6. Si hay artefactos, guardar el log de OptiScaler, una captura/vídeo corto y
   los datos de versión/ruta de Streamline y `nvngx_dlssg.dll`.

No se debe considerar funcional MFG solo porque el menú permita escoger un
valor: deben aumentar las presentaciones reales y mantenerse estable la imagen.

## Si reaparecen barras negras o líneas rápidas

El patrón de líneas negras que se vuelve más rápido al subir de x3 a x4 suele
indicar que el proveedor genera más subframes, pero recibe historia temporal,
HUD/UI o tags de recursos incompatibles. El defecto se amplifica porque hay más
subframes entre dos imágenes reales.

La siguiente fase no sería volver a introducir FSR como fallback. Sería portar
de forma nativa a OptiScaler las partes relevantes de MFGAdaUnlock-RenoDx:

1. **Automatic Guard + UI Composition**: validar los pares HUD-less/UI antes de
   enviarlos a DLSS-G.
2. Rechazar únicamente tags opcionales no válidos y usar final color de forma
   conservadora, preservando color, profundidad y vectores requeridos.
3. Elegir una ruta segura para HDR cuando el espacio de color no se pueda
   comprobar.
4. Hacer un reset temporal único después de transiciones de HDR, swapchain,
   resolución, calidad o multiplicador.
5. Añadir telemetría de `slDLSSGSetOptions`, `slDLSSGGetState` y presentaciones
   reales para distinguir un selector activo de MFG efectivo.
6. Comparar controladamente la ruta nativa de OptiScaler contra ReShade en
   Wukong antes de crear perfiles específicos por juego.

Esto se implementaría como código interno de OptiScaler: sin requerir ReShade,
sin cargar su interfaz o DLL, y manteniendo la atribución/licencia aplicable.

## Próxima decisión técnica

Primero se debe probar Wukong y Silent Hill 2. Si Wukong confirma barras negras
con un proveedor que sí quedó validado, la prioridad es portar el guard de
inputs/UI Composition antes de investigar DLSSG SM86/SM75 para RTX 30/20.
