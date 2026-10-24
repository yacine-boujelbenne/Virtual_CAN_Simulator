# CHECKPOINT — Session du 21/10/2026 (P0.6 terminé — roadmap P0 complète)

Reprise : relire ce fichier + `git log -1`. Le plan approuvé est P0 → P0.6 + gates (P1/P2 ensuite).

## État d'avancement

| Slice | État | Preuve |
|---|---|---|
| P0.0 Environnement | ⚠ partiel | Build g++ manuel vert, tests Python verts, 42 tests C++ verts (shim gtest local). **Bloqué : docker (permission denied) + cmake/libgtest absents** — voir « Actions utilisateur » |
| P0.1 `event-envelope.v2.json` | ✅ terminé | Draft 2020-12, 3 checks jsonschema |
| P0.2 Enveloppe timestamps + latence E2E | ✅ terminé + pérennisé | `python3 tests/test_envelopes.py` → 4/4 OK ; régression `test_web` 8/8 + `test_distributed` 5/5 (mode `run_local.py`) |
| P0.3 ADAS multi-objets + fusion | ✅ terminé | `python3 tests/test_adas_fusion.py` → 5/5 OK ; layout TX/RX identique, 4 slots ; CI vérifie désormais les tests natifs |
| P0.4 ISO-TP 100 % C++ | ✅ terminé | `python3 tests/test_isotp_vectors.py` → 13 vecteurs OK (C++ ↔ oracle Python) ; `tests/test_isotp.cpp` → 12 cas dans `ctest` ; CI link corrigé |
| P0.5 `.vcanproj` + runs A/B | ✅ terminé | `python3 tests/test_project_ab.py` → 8/8 OK (natif + HTTP) ; hash SHA-256 canonique dans le noyau C++ |
| P0.6 Onglet Performance dashboard | ✅ terminé | `python3 tests/test_performance.py` → 7/7 OK ; section `performance` du snapshot + 6ᵉ onglet UI |
| Gates G1 (vecteurs protocoles) / G2 (A/B reproductible) | G1 ✅ / G2 ✅ | G1 : `tests/vectors/isotp` + test natif ; G2 : `tests/test_project_ab.py` |

Tests verts au moment du checkpoint :
- natifs : `tests/test_envelopes.py` (4), `tests/test_protocols.py` (6), `tests/test_adas_fusion.py` (5), `tests/test_isotp_vectors.py` (13 vecteurs), `tests/test_project_ab.py` (8), `tests/test_performance.py` (7), C++ (42 via shim gtest) ;
- cluster local : `tests/test_web.py` (8) + `tests/test_distributed.py` (5) + `tests/test_project_ab.py` (HTTP) via `python3 app/run_local.py`.
`tests/test_container_failure.py` n'est valide qu'en CI après `docker compose stop ecu-motor` (échec standalone = attendu).

## Décisions verrouillées (réponses utilisateur)
B1=a (CAN XL), B2=d (J1939/DoIP → P1/P2), B3=a (multi-objets + fusion + E2E) ; A1=c, A2=c (IA = agent
offline, application manuelle), A3=b, A4=c (AI_ENDPOINT + Ollama), A5=a, C1=a, C2=oui (deadline jeu. 22/10),
C3=c, C4=c (seul format `.vcanproj`), C5=3 + Q4=oui (vecteurs de conformité = 2e gate), Q1=oui (découpage),
Q-A=Docker, **Q-B=ISO-TP tout en C++** (le Python `app/transport.py` devient oracle/test).
Recul si jeudi déborde : rapport P0.6 → P0.5 → P0.4 en priorité (P0.1–P0.3 gardés).
Règles addendum B : tous les `*_at_us` sont des µs entiers de simulation ; monotonicité
`sampled ≤ computed ≤ received ≤ observed ≤ applied` ; jamais de mélange horloge murale/simulation ; un
frame ne peut toucher le bus avant `computed_at_us` de son producteur ; tick rejeté si `timing` absent ou
hors fenêtre.

## P0.2 — ce qui a été fait (résumé technique)
- `resources/schemas/event-envelope.v2.json` : contrat v2 (`$defs/timestamps`, `quality_reason`, contraintes de monotonicité).
- `headers/SimulationManager.hpp` : champs `Ecu.timing`, anneaux `envelopes_` **et** `actuations_` (séparés,
  sinon noyé par ~17 acquisitions/tick), `frameSeq_`, `pushEnvelope/pushActuation`.
- `src/SimulationManager.cpp` :
  - `prepareTick` : `sampled_at_us`/`validity` par capteur ; `delivered_at_us`/`delivered_to` sur les frames reçues ;
    enveloppe `can_frame`.
  - `completeTick` : validation obligatoire du `timing` (`sampled==tickUs`, `computed ∈ [tickUs, tickUs+step*1000]`),
    enveloppe d'actuation (tick boundary : `applied = tickUs + step` pour respecter `computed ≤ applied`).
  - `transmit` : `seq`, planification bornée à `max(tickUs, producer.computed_at_us, busFree)` (fix delivered<computed),
    enveloppe `dropped_frame`, `scheduled_` enrichi.
  - `snapshot` : `timing = {contract_version, envelopes, actuations, frame_seq}` ; `data_age_us` par capteur.
  - `EcuKernel::tick` : `result.timing = {sampled_at_us, computed_at_us = sampled + ceil(cost), task_cost_us, ...}`.
- Test permanent `tests/test_envelopes.py` (4 cas : contrat barrière, validité+monotonicité, computed≤delivered,
  robustesse actuation/drop face au flood d'acquisitions).

## P0.3 — ce qui a été fait (multi-objets + fusion)
**Layout RadarObjectList 0x400 (FD 32 B), 19 signaux, slots de 56 bits (dernier bit 232/256) :**
- En-tête : `object_count` @0|8, `sample_counter` @8|8, `radar_valid` @16|1 ; pad 17–23.
- Slot i (i=0..3), base = `24 + i*56` : `obj{i}_range` base|16 (f=0.01, max 600), `obj{i}_closing_speed`
  base+16|16 (f=0.01, max 80), `obj{i}_confidence` base+32|8 (max 100), `obj{i}_valid` base+40|1.
- TX radar et RX ADAS déclarent **le même tableau de signaux** (validateur `tx==rx` du contrôleur) ;
  le test compare le décodage côté contrôleur et côté noyau ADAS, bit à bit.

**Monde (3 éditions C++ `src/SimulationManager.cpp`) :**
1. `sensorSources` += `static_object_range` ; `allowedInputs` += `static_object_sensor`, `front_perception_sensor`.
2. Défauts `world_` += `{"static_object_s",150},{"static_object_range",150}` (avant le load des sensors, qui fait `world_.at(source)`).
3. Plante += `world_["static_object_range"] = max(0, static_object_s - position)` (obstacle fixe sur la piste).

**Scénario `resources/scenarios/ev_basic.json`** : 2 capteurs ajoutés — `static_object_sensor`
(source `static_object_range`, 0..500, 10 ms, noise 0) et `front_perception_sensor` (source `lead_distance`,
0..600, 20 ms, noise 0.05 = 2ᵉ chemin de mesure indépendant de la même scène).
`disconnected` est clampé au max → validité via `lt(x, max)` strict.

**`resources/ecu_templates/radar.json`** : inputs += `static_object_sensor`, `speed_sensor` (obj1 closing = vitesse ego) ;
variables `obj0..obj3_range/closing_speed/confidence/valid` + `object_count`, `sample_counter`, `radar_valid`, `radar_fault`
(obj2/obj3 jamais assignés = slots invalides) ; tâches `radar_sampling` (obj0 ← lead, obj1 ← statique + vitesse ego,
`radar_fault`) puis `radar_validity` (`obj0_valid = eq(radar_fault,0)`, `obj1_valid = lt(static_object_sensor,500)`,
`object_count = add(eq(radar_fault,0), lt(static_object_sensor,500))`, `sample_counter` incrémenté avec wrap 255→0).

**`resources/ecu_templates/adas.json`** : inputs renommés `object_*` → `obj0_*` + `obj0_valid`, `object_count`,
`sample_counter`, `front_perception_sensor`, `obj1..3_*` ; variables += `perception_gap`, `fusion_disagree`,
`perception_valid`. Pipeline de tâches **consécutives** (voir « écarts » ci-dessous) :
`fusion` (`perception_gap = max(sub(obj0_range,front), sub(front,obj0_range))`, TTC, `adas_fault`, `startup_ms`) →
`fusion_consistency` (`fusion_disagree = gt(perception_gap,5)`) → `perception_validity`
(`perception_valid = and(eq(radar_valid,1), eq(fusion_disagree,0))`) → `collision_warning`
(`fcw` inchangé, `aeb_request` **gaté** sur `perception_valid`) → `cruise_control`.

**Écarts assumés vs design figé (voir commit) :**
1. `perception_valid` **ne peut pas** vivre dans `collision_warning` : dans une tâche, toutes les affectations
   lisent le même instantané pré-tâche (`EcuKernel::tick`), donc `aeb_request` l'aurait lu avec une valeur
   d'un tick de retard (=0) et l'AEB n'aurait jamais.fire. D'où la scission en tâches consécutives
   (`fusion_consistency` puis `perception_validity`) : chaque tâche voit le résultat de la précédente
   **dans le même tick**, donc aucun retard. Règle désormais documentée dans `docs/ARCHITECTURE.md`.
2. `obj0_valid = eq(radar_fault,0)` (et non `lt(static_object_sensor,500)`) : sinon `object_count` (=1) serait
   incohérent avec le slot 0 invalide lors d'un `disconnected` du capteur statique.
3. La péremption (staleness) se déclenche en **drop** du trafic radar, pas en latence : l'âge d'entrée
   (`freshness_inputs`) est mesuré depuis le tick de **réception**, une latence supplémentaire ne vieillit donc
   pas l'entrée. Un bus radar muet > 100 ms ⇒ `adas_fault` ⇒ `fcw=0` et `aeb_request=0`.
4. `tests/test_kernel.cpp` : l'assertion « signal au-dessus du bit 64 » visait l'ancien layout
   (`data[10]==100`) ; elle pointe maintenant le slot 1 (`data[10]==152` pour `static_object_sensor=150`).
5. `web/` : nœud radar = `obj0_range`/`obj0_closing_speed`/nombre d'objets, badge « FUSION HOLD » côté ADAS,
   nouvelle stat « Perception fusion » (écart en m) dans la scène.

**Test permanent `tests/test_adas_fusion.py` (5 cas)** : décodage identique TX/RX des 4 slots + `count=2` ;
capteur statique `disconnected` ⇒ `obj1_valid=0`, `count=1`, slot 0 intact ; désaccord de fusion (biais 10 m)
⇒ `fusion_disagree=1`, `perception_valid=0`, `aeb_request=0` **et** `fcw=1` (avertissement conservateur) ;
trafic radar perdu ⇒ `radar_age_ms>100`, `adas_fault=1`, `fcw=0`, état `FAULT`, puis récupération ;
capteur radar bloqué à 700 m ⇒ `radar_fault=1`, `radar_valid=0`, `count=1`, DTC `C1A10`, `adas_fault=1`.

## P0.4 — ce qui a été fait (ISO-TP 100 % C++)
**Moteur C++ (`headers/IsoTp.hpp`, `src/IsoTp.cpp`)** : session ISO-TP événementielle (ISO 15765-2)
qui ne touche jamais au bus — le propriétaire lui fournit les trames et republie celles qu'elle
retourne. La même session sert au chemin embarqué, au chemin distribué (worker) et au testeur.

- Adressage normal **ou étendu 11 bits** (octets source/cible devant le PCI, vérifiés à la réception).
- CAN classique (8 B) et **CAN FD** (longueurs légales jusqu'à 64 B, trame simple échappée pour
  8–62 B).
- Segmentation : FF / FC / CF, **BS et STmin respectés**, numéros de séquence enroulés à 15.
- PDU 1–4095 B ; timeouts **N_As / N_Bs / N_Ar / N_Cr** évalués sur l'horloge de session
  (tick de simulation ou temps fourni par l'appelant).
- Erreurs rejetées **avant** toute mutation d'état : trame simple/première trame malformée, erreur
  de séquence, FC inattendu, FC avorté, limite de 3 wait frames, STmin réservé.

**Adaptateurs** : `CanTp` (endpoint bloquant) réécrit au-dessus de la session ; `app/worker.py` et
`app/controller.py` ne transportent plus que des trames (la machine à états est dans le noyau C++).

**Oracle** : `app/transport.py` devient l'**oracle** de référence (horloge injectable), plus une
implémentation de production. Les vecteurs `tests/vectors/isotp/*.json` (13) sont rejoués deux fois
— moteur C++ et oracle — et doivent produire les mêmes trames, messages et échecs
(`tests/test_isotp_vectors.py`). `tests/test_isotp.cpp` rejoue les mêmes scénarios nativement dans
`ctest` (12 cas).

**Bug trouvé et corrigé pendant la slice** : la session C++ ne libérait pas l'émetteur après la
dernière trame CF (`tx_.active` restait `true`) → l'endpoint bloquant `CanTp` expirait en timeout
(3 tests C++ en échec) et une seconde transmission sur la même session était refusée à tort.
Correctif : `src/IsoTp.cpp` (bloc flow control) ; verrouillé par le vecteur
`tests/vectors/isotp/sequential_transfers.json` (vérifié : il échoue sans le correctif).

**CI** : `.github/workflows/container.yml` compilait sans `src/IsoTp.cpp` → **link cassé** depuis
l'introduction de la session ; corrigé, et `tests/test_isotp_vectors.py` ajouté à l'étape native.

## P0.5 — ce qui a été fait (`.vcanproj` + runs A/B)
**Format projet** (un seul `.vcanproj`, C4=c) : `{kind:"vcanproj", schema_version:1, name, project_hash,
scenario, ecu_definitions, can_map, run, runtime}`. `ecu_definitions` est indexé par le nom de fichier
référencé par le scénario (clé portable : le scénario voyage avec ses définitions) ; `can_map` est
**dérivé** des définitions (ECU, message, ID, direction, bus, DLC, FD, période, signaux) ; `run` =
`{step_ms, bitrate, policies}`.

**Hash** : SHA-256 (**implémenté en C++**, `headers/Sha256.hpp` + `src/Sha256.cpp`, vérifié contre
`sha256sum`/`hashlib` sur 4 vecteurs) sur la forme canonique (JSON trié, sans espaces) de
`{scenario, ecu_definitions, can_map, run}`. Les **état live** (contrôles, monde, horloge) sont hors
hash : deux runs de la même config partagent le même hash, une config modifiée change de hash.

**Noyau C++** (`src/SimulationManager.cpp`) : `reset()` devient `applyScenario(scenario, definitions)` ;
nouveau membre `definitions_` (définitions chargées, clé = fichier). Actions : `export_project`,
`import_project`, `hash_project` (recalcul du hash d'un bundle, pour outiller un projet édité).
`import_project` est **strict** (hash déclaré ≠ hash recalculé ⇒ refus avec les deux valeurs) et
**transactionnel** (scénario + définitions précédents restaurés si l'import échoue) ; les policies de
flux du projet sont réappliquées. `snapshot` expose désormais `project_hash`.

**Contrôleur / API** : `app/controller.py` forwarde `export_project` / `import_project` ; en mode
distribué, l'import exige un **endpoint dédié par ECU importé** (vérifié avant toute modification) et
reconfigure les workers ; les ECUs retirés du projet repassent en réserve. `app/server.py` :
`GET /api/project` (export) et `POST /api/project` (import, 400 si projet invalide).

**UI** : panneau « PROJECT FILE · .VCANPROJ » dans l'onglet ECU builder (export `.vcanproj`, import
d'un fichier, hash affiché en direct depuis `snapshot`).

**Test natif** : `tests/test_sha256.cpp` (3 cas : vecteurs connus FIPS 180-4, 1 Mo, frontières de
bloc) — le SHA-256 C++ est donc vérifié dans `ctest`, pas seulement en Python.

**Gate G2** (`tests/test_project_ab.py`, 8 cas) : export → import dans un **cœur neuf** ⇒ hash identique ;
**signature de run identique** (trames + enveloppes) sur 60 ticks ; projet modifié re-haché ⇒ hash
différent ; projet altéré sans re-hachage ⇒ **refusé** et état intact ; projet sans définitions ⇒ refusé ;
idempotence côté HTTP (import/export, 400 sur projet altéré).

## P0.6 — ce qui a été fait (onglet Performance)
**Mesures dans le noyau C++** (le noyau possède l'horloge de simulation, donc il mesure) :
- Fenêtre glissante par ECU (600 échantillons, 300 points de timeline) : coût de tâche
  (`computed_at_us - sampled_at_us`), latence de livraison (`computed` → livraison sur le bus, attribuée au
  producteur), latence d'actuation (`computed` → commit plante), chaîne de contrôle (`sampled` → `applied`).
- Compteurs : tx/rx par ECU, trames et pertes par bus, charge estimée, backlog, débit.
- `snapshot.performance` : `{ecus:[{task,e2e,delivery,actuation,tx,rx,dropped,states,timeline}], buses:[...],
  totals:{frames,dropped,frame_rate_hz,task_p95_us,delivery_p95_us,chain_p95_us,ticks,window_s}}`.
- Percentiles en nearest-rank ; fenêtre **remise à zéro** au reset/changement de scénario.

**UI** : 6ᵉ onglet « 06 Performance » (rail + barre d'onglets) : 4 cartes KPI (chaîne E2E, tâche p95,
livraison p95, débit), tableau par ECU (p50/p95/max de la tâche, livraison p95, chaîne p95, tx/rx/dropped/
états + jauge), panneau bus (charge, débit, backlog, pertes) et **timeline 30 s** (coût de tâche + latence de
livraison) en SVG.

**Effet mesurable** : avec une règle de flux `{bus:adas, latency_ms:5}`, le p95 de livraison passe de ~0.3 ms
à ~5.3 ms pour Radar/ADAS alors que le coût de tâche reste plat — la décomposition de latence est donc
exploitable (validé sur le cluster distribué).

**Test permanent `tests/test_performance.py` (7 cas)** : présence de la fenêtre, ordre des percentiles,
chaîne quantifiée par la barrière (10 ms = tâche + actuation), accounting des pertes, monotonie de la
timeline, remise à zéro de la fenêtre au reset.

## Comment on lance les choses (natif, pas de docker)
- Build : `g++ -std=c++20 -Iheaders -O2 -Wall -Wextra src/Can.cpp src/CanBus.cpp src/IsoTp.cpp src/CanTp.cpp src/Sender.cpp src/Receiver.cpp src/Sha256.cpp src/SimulationManager.cpp app/main.cpp -o build/vehicle_sim -lpthread`
  (et `app/ecu_main.cpp` → `build/ecu_kernel`) ; ~2–3 min, 0 warning attendu.
- Tests C++ natifs : `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build`
  (sans libgtest : shim local dans `/tmp/opencode/gtest`, 42 tests).
- Cluster distribué sans docker : `python3 app/run_local.py` (8 workers + controller sur 127.0.0.1:8080) ;
  arrêt propre par SIGTERM. `tests/test_web.py` et `tests/test_distributed.py` s'exécutent dessus
  (`TEST_URL=http://127.0.0.1:8080`).
- Serveur embedded (mode sans workers) : `python3 app/server.py` — **sait que `diagnostic` y retourne un
  snapshot et non `bytes`** (comportement préexistant, `test_web` est conçu pour le distribué).
- Les binaires doivent exister pour `tests/test_envelopes.py`, `tests/test_adas_fusion.py`,
  `tests/test_isotp_vectors.py` et `tests/test_project_ab.py` (sinon skip).
- CI (`.github/workflows/container.yml`) : step « Native build and envelope/perception checks »
  (build g++ + `test_envelopes.py` + `test_adas_fusion.py` + `test_isotp_vectors.py` +
  `test_project_ab.py`) avant le build Docker ; `test_project_ab.py` est aussi rejoué sur le cluster
  (étape « HTTP application integration »).

## Actions utilisateur en attente (bloquent P0.0 et la CI locale)
```
sudo apt-get install -y cmake libgtest-dev
sudo usermod -aG docker $USER   # puis se déconnecter/reloguer
```
Puis : `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build`.
Ollama est prêt (`llama3.2:1b`, `llama3.2:3b`). PDF de référence : `docs/ECU_Lab_Driving_Coach_Implementation_Study.pdf`
(la « companion pack » qu'il cite — schemas, `.vcanproj`, CAN map — n'existe pas dans le repo, il faut la créer).

## Suites immédiates (ordre)
1. **P1/P2** : CAN XL (B1=a), DoIP/J1939, IA agent (A2=c), `.vcanproj` signé/chiffré si besoin.
2. **Dette connue** : le test DOM `tests/test_ui.cjs` n'est pas rejoué en local (node absent) — il tourne
   en CI ; le hash projet et la fenêtre performance n'ont pas encore d'UI test jsdom.

Décisions P0.5 **prises** (documentées ci-dessus) : bundle = scénario + définitions + carte CAN
dérivée + config de run ; hash = SHA-256 C++ sur la forme canonique, stocké dans la section projet
(`project_hash`) **et** exposé par `snapshot` ; signature A/B = **trames + enveloppes** (les deux).
