# CHECKPOINT — Session du 20/10/2026 (fin de journée)

Reprise : relire ce fichier + `git log -1`. Le plan approuvé est P0 → P0.6 + gates (P1/P2 ensuite).

## État d'avancement

| Slice | État | Preuve |
|---|---|---|
| P0.0 Environnement | ⚠ partiel | Build g++ manuel vert, tests Python verts. **Bloqué : docker (permission denied) + cmake/libgtest absents** — voir « Actions utilisateur » |
| P0.1 `event-envelope.v2.json` | ✅ terminé | Draft 2020-12, 3 checks jsonschema |
| P0.2 Enveloppe timestamps + latence E2E | ✅ terminé + pérennisé | `python3 tests/test_envelopes.py` → 4/4 OK ; régression `test_web` 8/8 + `test_distributed` 5/5 (mode `run_local.py`) |
| P0.3 ADAS multi-objets + fusion | 🔄 analyse terminée, **aucun fichier édité** | Design figé ci-dessous |
| P0.4 ISO-TP 100 % C++ | ⬜ à faire | |
| P0.5 `.vcanproj` + runs A/B | ⬜ à faire | |
| P0.6 Onglet Performance dashboard | ⬜ à faire | |
| Gates G1 (vecteurs protocoles) / G2 (A/B reproductible) | ⬜ à faire | |

Tests verts au moment du checkpoint :
`tests/test_envelopes.py` (4), `tests/test_protocols.py` (6), `tests/test_web.py` (8, via `run_local.py`),
`tests/test_distributed.py` (5). `tests/test_container_failure.py` n'est valide qu'en CI après
`docker compose stop ecu-motor` (échec standalone = attendu).

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
- `resources/schemas/event-envelope.v2.json` : contrat v2 (`$defs/timestamps`, `quality_reason`, contraintes de monoticité).
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
- Test permanent `tests/test_envelopes.py` (4 cas : contrat barrière, validité+monoticité, computed≤delivered,
  robustesse actuation/drop face au flood d'acquisitions).

## P0.3 — design prêt à implémenter (aucune édition faite)
**Layout RadarObjectList 0x400 (FD 32 B), 19 signaux, slots de 56 bits :**
- En-tête : `object_count` @0|8, `sample_counter` @8|8, `radar_valid` @16|1 ; pad 17–23.
- Slot i (i=0..3), base = `24 + i*56` : `obj{i}_range` base|16 (f=0.01, max 600), `obj{i}_closing_speed`
  base+16|16 (f=0.01, max 80), `obj{i}_confidence` base+32|8 (max 100), `obj{i}_valid` base+40|1.
  Fin = 233 ≤ 256 ✓. TX radar et RX ADAS doivent être strictement identiques (validateur `tx==rx`).

**Monde (3 éditions C++ `src/SimulationManager.cpp`) :**
1. `sensorSources` (:93) += `static_object_range` ; `allowedInputs` (:94) += `static_object_sensor`, `front_perception_sensor`.
2. Défauts `world_` (:206) += `{"static_object_s",150},{"static_object_range",150}` (avant le load des sensors, qui fait `world_.at(source)`).
3. Plante (~:387) += `world_["static_object_range"] = max(0, static_object_s - position)`.
Sens `front_perception_sensor` = `lead_distance` (vérité partagée, 2ᵉ chemin de mesure indépendant, period 20 ms, noise 0.05).

**`resources/scenarios/ev_basic.json`** : ajouter 2 capteurs :
`static_object_sensor` (source `static_object_range`, 0..500, 10 ms, noise 0) et
`front_perception_sensor` (source `lead_distance`, 0..600, 20 ms, noise 0.05).
Note : `disconnected` est clampé au max → validité via `lt(x, max)` strict.

**`resources/ecu_templates/radar.json`** :
- inputs += `static_object_sensor`, `speed_sensor` (obj1 closing = vitesse ego).
- variables : `obj0..obj3_range/closing_speed/confidence/valid` + `object_count`, `sample_counter`, `radar_valid`, `radar_fault` (obj2/obj3 init 0, jamais assignés = slots invalides).
- tâches : `radar_sampling` (obj0 ← capteurs lead ; obj1 ← static + speed_sensor ; radar_fault inchangé) ;
  `radar_validity` (obj0_valid/obj1_valid = `lt(static_object_sensor,500)` ; `object_count` calculé **sans retard**
  = `add(eq(radar_fault,0), lt(static_object_sensor,500))` ; `sample_counter` = `if(gt(...,254),0,add(...,1))`).
- message tx → 19 signaux (layout ci-dessus).

**`resources/ecu_templates/adas.json`** :
- inputs : renommer `object_range→obj0_range`, `object_closing_speed→obj0_closing_speed`,
  `object_confidence→obj0_confidence` ; += `obj0_valid`, `object_count`, `sample_counter`, `front_perception_sensor`.
- variables += `perception_gap: 99`, `fusion_disagree: 0`, `perception_valid: 0`.
- tâche `fusion` += `perception_gap = max(sub(obj0_range,front), sub(front,obj0_range))` (abs sans op abs) et
  `fusion_disagree = gt(perception_gap, 5)`.
- tâche `collision_warning` += `perception_valid = and(eq(radar_valid,1), eq(fusion_disagree,0))`
  (les tâches s'appliquent immédiatement dans l'ordre, donc pas de retard) ; **gater `aeb_request` sur
  `perception_valid`** (FCW inchangé = avertissement conservateur). Ne casse pas l'AEB de test_web (gap≈0).
- rx RadarObjectList → 19 signaux identiques au tx.

**`web/app.js:108`** : `v.object_range→v.obj0_range`, `v.object_closing_speed→v.obj0_closing_speed`
(seule référence externe aux anciens noms — vérifié par grep).

**Test P0.3 à créer** (`tests/test_adas_fusion.py`, style `test_envelopes.py`) : décodage des 4 slots à l'ADAS,
count=2 en fonctionnement, staleness (radar âgé >100 ms → `adas_fault` → fcw=0), désaccord fusion
(fault injection `bias` sur `front_perception_sensor` → `fusion_disagree=1` → `aeb_request` supprimé),
obj1 invalidé par `disconnected` (clamp 500 → `lt` faux → valid=0, count=1).

## Comment on lance les choses (natif, pas de docker)
- Build : `g++ -std=c++20 -Iheaders -O2 -Wall -Wextra src/Can.cpp src/CanBus.cpp src/CanTp.cpp src/Sender.cpp src/Receiver.cpp src/SimulationManager.cpp app/main.cpp -o build/vehicle_sim -lpthread`
  (et `app/ecu_main.cpp` → `build/ecu_kernel`) ; ~2–3 min, 0 warning attendu.
- Cluster distribué sans docker : `python3 app/run_local.py` (8 workers + controller sur 127.0.0.1:8080) ;
  arrêt propre par SIGTERM. `tests/test_web.py` et `tests/test_distributed.py` s'exécutent dessus
  (`TEST_URL=http://127.0.0.1:8080`).
- Serveur embedded (mode sans workers) : `python3 app/server.py` — **sait que `diagnostic` y retourne un
  snapshot et non `bytes`** (comportement préexistant, `test_web` est conçu pour le distribué).
- Les binaires doivent exister pour `tests/test_envelopes.py` (sinon skip).

## Actions utilisateur en attente (bloquent P0.0 et la CI locale)
```
sudo apt-get install -y cmake libgtest-dev
sudo usermod -aG docker $USER   # puis se déconnecter/reloguer
```
Puis : `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build`.
Ollama est prêt (`llama3.2:1b`, `llama3.2:3b`). PDF de référence : `docs/ECU_Lab_Driving_Coach_Implementation_Study.pdf`
(la « companion pack » qu'il cite — schemas, `.vcanproj`, CAN map — n'existe pas dans le repo, il faut la créer).

## Suites immédiates (ordre)
1. Implémenter P0.3 tel que figé ci-dessus → rebuild → `test_adas_fusion.py` + régression complète → commit.
2. P0.4 ISO-TP C++ (sessions event-driven, addressing étendu, timeouts N_*, `tests/vectors/` comme oracle).
3. P0.5 `.vcanproj` (export/import + hash) ; P0.6 onglet Performance ; gates G1/G2 + CI.
