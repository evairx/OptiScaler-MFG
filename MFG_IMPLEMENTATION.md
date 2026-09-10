# NVIDIA MFG: implementación y plan de validación

## Estado de esta implementación

La implementación actual está en el commit `908cc81f` (`main`). La compilación
Release x64 se validó correctamente en GitHub Actions. Esto verifica que el
código compila, pero no sustituye pruebas en juegos reales.

El objetivo de este cambio es habilitar **NVIDIA DLSS Multi Frame Generation
real** en RTX 40/Ada, sin anunciar multiplicadores que el proveedor DLSS-G no
ha aceptado realmente.

## Alcance actual

| Ruta | Estado |
| --- | --- |
| RTX 40/Ada, DLSS-G/MFG x2–x6 | Implementada, con validación del proveedor en tiempo de ejecución |
| RTX 30/SM86 y RTX 20/SM75 | No integrada; no se presenta como MFG real |
| OptiFG como input | Se mantiene y puede alimentar NVIDIA DLSS MFG en RTX 40, FSR FG standalone o Intel XeFG |
| FSR FG standalone | Se mantiene como salida explícita |
| Intel XeFG | Se mantiene como salida explícita |
| Fallback automático MFG → FSR | Eliminado intencionadamente |

La opción **Unlock MFG** queda desactivada por defecto. El usuario debe
activarla y reiniciar cuando el menú lo solicite.

## Qué se cambió

### Validación de MFG Ada

`AdaMFGUnlock` ahora solo intenta el desbloqueo en una GPU NVIDIA Ada/RTX 40.
Las rutas experimentales que reescribían PTX para SM86/SM75 se eliminaron: no
deben presentarse como MFG Ada real en RTX 30/20.

Antes de habilitar x3/x4/x6 se exigen los tres resultados siguientes:

1. Se aplicó el parche de arquitectura del proveedor `nvngx_dlssg`.
2. Se aplicó la corrección temporal de midpoint/PTX de Ada.
3. Se localizó y desbloqueó el techo de `numFramesToGenerate` del plugin
   Streamline DLSS-G.

El tercer punto es importante: la revisión anterior podía mostrar x3/x4/x6,
pero el plugin conservaba internamente el límite x2. El nuevo código no expone
un máximo fijo de 5; utiliza el máximo real detectado en el módulo activo.

### Streamline y OptiFG

Las rutas `slDLSSGSetOptions` y `slDLSSGGetState` se ajustaron para:

- Solicitar multiplicadores superiores solo después de una validación exitosa.
- Reintentar una vez un rechazo transitorio del proveedor.
- Restaurar la petición nativa del juego si el proveedor rechaza MFG, en vez de
  cambiar de backend o de forzar FSR.
- Publicar el máximo verificado también mediante `DLSSG.MultiFrameCountMax`.
- Mantener la misma protección en la ruta de instancia propia de OptiFG hacia
  DLSS-G.

El menú muestra el proveedor como pendiente hasta que la validación se completa
y deshabilita los multiplicadores no verificados.

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
