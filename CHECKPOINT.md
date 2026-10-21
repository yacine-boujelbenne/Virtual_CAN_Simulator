# CHECKPOINT — Session du 21/10/2026 (P0.3 terminé)

Reprise : relire ce fichier + `git log -1`. Le plan approuvé est P0 → P0.6 + gates (P1/P2 ensuite).

## État d'avancement

| Slice | État | Preuve |
|---|---|---|
| P0.0 Environnement | ⚠ partiel | Build g++ manuel vert, tests Python verts, 27 tests C++ verts (shim gtest local). **Bloqué : docker (permission denied) + cmake/libgtest absents** — voir « Actions utilisateur » |
| P0.1 `event-envelope.v2.json` | ✅ terminé | Draft 2020-12, 3 checks jsonschema |
| P0.2 Enveloppe timestamps + latence E2E | ✅ terminé + pérennisé | `python3 tests/test_envelopes.py` → 4/4 OK ; régression `test_web` 8/8 + `test_distributed` 5/5 (mode `run_local.py`) |
| P0.3 ADAS multi-objets + fusion | ✅ terminé | `python3 tests/test_adas_fusion.py` → 5/5 OK ; layout TX/RX identique, 4 slots ; CI vérifie désormais les tests natifs |
| P0.4 ISO-TP 100 % C++ | ⬜ à faire | |
| P0.5 `.vcanproj` + runs A/B | ⬜ à faire | |
| P0.6 Onglet Performance dashboard | ⬜ à faire | |
| Gates G1 (vecteurs protocoles) / G2 (A/B reproductible) | ⬜ à faire | |

Tests verts au moment du checkpoint :
- natifs : `tests/test_envelopes.py` (4), `tests/test_protocols.py` (6), `tests/test_adas_fusion.py` (5), C++ (27 via shim gtest) ;
- cluster local : `tests/test_web.py` (8) + `tests/test_distributed.py` (5) via `python3 app/run_local.py`.
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

## Comment on lance les choses (natif, pas de docker)
- Build : `g++ -std=c++20 -Iheaders -O2 -Wall -Wextra src/Can.cpp src/CanBus.cpp src/CanTp.cpp src/Sender.cpp src/Receiver.cpp src/SimulationManager.cpp app/main.cpp -o build/vehicle_sim -lpthread`
  (et `app/ecu_main.cpp` → `build/ecu_kernel`) ; ~2–3 min, 0 warning attendu.
- Cluster distribué sans docker : `python3 app/run_local.py` (8 workers + controller sur 127.0.0.1:8080) ;
  arrêt propre par SIGTERM. `tests/test_web.py` et `tests/test_distributed.py` s'exécutent dessus
  (`TEST_URL=http://127.0.0.1:8080`).
- Serveur embedded (mode sans workers) : `python3 app/server.py` — **sait que `diagnostic` y retourne un
  snapshot et non `bytes`** (comportement préexistant, `test_web` est conçu pour le distribué).
- Les binaires doivent exister pour `tests/test_envelopes.py` et `tests/test_adas_fusion.py` (sinon skip).
- CI (`.github/workflows/container.yml`) : nouveau step « Native build and envelope/perception checks »
  (build g++ + `test_envelopes.py` + `test_adas_fusion.py`) avant le build Docker.

## Actions utilisateur en attente (bloquent P0.0 et la CI locale)
```
sudo apt-get install -y cmake libgtest-dev
sudo usermod -aG docker $USER   # puis se déconnecter/reloguer
```
Puis : `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build`.
Ollama est prêt (`llama3.2:1b`, `llama3.2:3b`). PDF de référence : `docs/ECU_Lab_Driving_Coach_Implementation_Study.pdf`
(la « companion pack » qu'il cite — schemas, `.vcanproj`, CAN map — n'existe pas dans le repo, il faut la créer).

## Suites immédiates (ordre)
1. **P0.4 ISO-TP 100 % C++** : sessions event-driven, addressing étendu, timeouts N_*, `tests/vectors/` comme oracle
   (le Python `app/transport.py` devient le test de référence).
2. P0.5 `.vcanproj` (export/import + hash de scénario, runs A/B reproductibles).
3. P0.6 onglet Performance (latence E2E par ECU, bus load, envelopes). Gates G1 (vecteurs protocoles)
   / G2 (A/B reproductible) + CI.
