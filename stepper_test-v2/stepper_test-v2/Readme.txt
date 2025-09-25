Variables configurables (rápido y claro)

DIR=CW | DIR=CCW
Sentido principal de giro lógico.

Aumentar/Disminuir: cambia el signo del avance. Úsalo para definir el “sentido normal” del ciclo.

DIRPOL=NORM | DIRPOL=INV
Polaridad del pin DIR.

NORM: CW→HIGH, CCW→LOW.

INV: invierte el nivel por si tu driver gira al revés de lo esperado.

Efecto: Corrige que BACKOFF realmente vaya “hacia atrás” y que el ciclo se mueva en el sentido deseado.

OFFSET=±xx.x (°)
Desplazamiento sensor → cero mecánico en el sentido principal.

↑(más positivo): el cero mecánico queda más adelante del sensor.

↓(negativo): el cero mecánico queda antes del sensor.

Efecto: Alinea el 0° real de tu máquina tras homing.

WIN=xx.x (°)
Semi-ancho de la ventana de aceptación en FINE (centrada en 0° relativo).

↑: acepta flancos más separados del centro (más tolerante).

↓: más estricto (necesita pasar muy cerca del centro).

Típico: 6–12°.

LOCK=xx.x (°)
Bloqueo (lockout) tras aceptar un índice.

↑: menos chance de doble conteo (útil si hay rebotes), pero tarda más en rearmar.

↓: rearmado más rápido (cuidado con flancos múltiples cercanos).

HREVMAX=xx.x (vueltas)
Vueltas máximas en COARSE/FINE antes de FAULT.

↑: tolera más recorrido antes de fallar.

↓: falla más pronto si no encuentra el índice.

L / M / R (°/s)
Velocidades lenta / media / rápida por sector.

↑: mayor throughput (cuidado con caída de huevos).

↓: más suave/seguro.

J (°/s³)
Jerk nominal de transición entre velocidades.

↑: cambios más “vivos”.

↓: más suave (mejor para no sacudir los huevos).

A (°/s²)
Aceleración máxima nominal.

↑: acelera/frena más fuerte.

↓: más progresivo.

JSTOP / ASTOP
Jerk/Accel para parada suave en FAULT/STOP!.

↑: frena más rápido (sin “seco” gracias a S-curve).

↓: freno más largo y suave.

HCOARSE / HFINE (°/s)
Velocidades de homing COARSE/FINE en sentido principal.

↑: homing más rápido; FINE muy alto puede pasar la ventana.

↓: homing más controlado (FINE suele ir entre 5–15 °/s).

HBACK (°)
Retroceso de homing entre COARSE y FINE.

↑: se aleja más del sensor antes de volver.

↓: ventana fina queda más cerca (si es muy bajo, podría no limpiar bien).

IDX=CHANGE | RISING | FALLING
Tipo de flanco a capturar.

CHANGE (recomendado): ambos flancos; la ventana decide.

RISING/FALLING: usa uno solo si lo necesitas por tu sensor.