#====================================================================================================
# START - Testing Protocol - DO NOT EDIT OR REMOVE THIS SECTION
#====================================================================================================

# THIS SECTION CONTAINS CRITICAL TESTING INSTRUCTIONS FOR BOTH AGENTS
# BOTH MAIN_AGENT AND TESTING_AGENT MUST PRESERVE THIS ENTIRE BLOCK

# Communication Protocol:
# If the `testing_agent` is available, main agent should delegate all testing tasks to it.
#
# You have access to a file called `test_result.md`. This file contains the complete testing state
# and history, and is the primary means of communication between main and the testing agent.
#
# Main and testing agents must follow this exact format to maintain testing data. 
# The testing data must be entered in yaml format Below is the data structure:
# 
## user_problem_statement: {problem_statement}
## backend:
##   - task: "Task name"
##     implemented: true
##     working: true  # or false or "NA"
##     file: "file_path.py"
##     stuck_count: 0
##     priority: "high"  # or "medium" or "low"
##     needs_retesting: false
##     status_history:
##         -working: true  # or false or "NA"
##         -agent: "main"  # or "testing" or "user"
##         -comment: "Detailed comment about status"
##
## frontend:
##   - task: "Task name"
##     implemented: true
##     working: true  # or false or "NA"
##     file: "file_path.js"
##     stuck_count: 0
##     priority: "high"  # or "medium" or "low"
##     needs_retesting: false
##     status_history:
##         -working: true  # or false or "NA"
##         -agent: "main"  # or "testing" or "user"
##         -comment: "Detailed comment about status"
##
## metadata:
##   created_by: "main_agent"
##   version: "1.0"
##   test_sequence: 0
##   run_ui: false
##
## test_plan:
##   current_focus:
##     - "Task name 1"
##     - "Task name 2"
##   stuck_tasks:
##     - "Task name with persistent issues"
##   test_all: false
##   test_priority: "high_first"  # or "sequential" or "stuck_first"
##
## agent_communication:
##     -agent: "main"  # or "testing" or "user"
##     -message: "Communication message between agents"

# Protocol Guidelines for Main agent
#
# 1. Update Test Result File Before Testing:
#    - Main agent must always update the `test_result.md` file before calling the testing agent
#    - Add implementation details to the status_history
#    - Set `needs_retesting` to true for tasks that need testing
#    - Update the `test_plan` section to guide testing priorities
#    - Add a message to `agent_communication` explaining what you've done
#
# 2. Incorporate User Feedback:
#    - When a user provides feedback that something is or isn't working, add this information to the relevant task's status_history
#    - Update the working status based on user feedback
#    - If a user reports an issue with a task that was marked as working, increment the stuck_count
#    - Whenever user reports issue in the app, if we have testing agent and task_result.md file so find the appropriate task for that and append in status_history of that task to contain the user concern and problem as well 
#
# 3. Track Stuck Tasks:
#    - Monitor which tasks have high stuck_count values or where you are fixing same issue again and again, analyze that when you read task_result.md
#    - For persistent issues, use websearch tool to find solutions
#    - Pay special attention to tasks in the stuck_tasks list
#    - When you fix an issue with a stuck task, don't reset the stuck_count until the testing agent confirms it's working
#
# 4. Provide Context to Testing Agent:
#    - When calling the testing agent, provide clear instructions about:
#      - Which tasks need testing (reference the test_plan)
#      - Any authentication details or configuration needed
#      - Specific test scenarios to focus on
#      - Any known issues or edge cases to verify
#
# 5. Call the testing agent with specific instructions referring to test_result.md
#
# IMPORTANT: Main agent must ALWAYS update test_result.md BEFORE calling the testing agent, as it relies on this file to understand what to test next.

#====================================================================================================
# END - Testing Protocol - DO NOT EDIT OR REMOVE THIS SECTION
#====================================================================================================



#====================================================================================================
# Testing Data - Main Agent and testing sub agent both should log testing data below this section
#====================================================================================================

user_problem_statement: |
  Améliorer la Rich Presence TriCord 3DS : au lieu d'afficher "Title 000400000F000200"
  brut sur Discord, montrer le vrai nom du jeu (SMDH), une icône personnalisée par jeu,
  un compteur de temps de jeu, et étendre la base titles.txt avec des entrées manuelles.
  Discord Application ID fourni par l'utilisateur : 1546529994421829693.

backend:
  - task: "Endpoints icônes/titres (Phase C)"
    implemented: true
    working: true
    file: "/app/backend/server.py"
    stuck_count: 0
    priority: "high"
    needs_retesting: false
    status_history:
        - working: "NA"
          agent: "main"
          comment: |
            Nouveaux endpoints :
             - POST /api/icons/{tid}  reçoit l'icône SMDH RGB565 (4608 octets)
               depuis le sysmodule, décode l'ordre Z SMDH → PNG 192×192 via Pillow,
               stocke /app/backend/files/icons/<TID>.png, indexe collection Mongo `titles`.
             - GET  /api/icons/{tid}.png   sert le PNG (cache 24h).
             - GET  /api/titles/{tid}   renvoie {tid, name, icon_url, source, updated_at}.
             - POST /api/titles/{tid}   upsert manuel (frontend futur).
            Aussi : cache CIA meta invalidé si mtime change (rebuild sans redémarrage).
            Tests locaux OK : POST 4608 octets → PNG 192x192, GET renvoie image, Mongo
            upsert visible via GET /api/titles/<TID>.
        - working: true
          agent: "testing"
          comment: |
            Comprehensive backend testing completed - ALL 17 TESTS PASSED:
            
            EXISTING ENDPOINTS (all working):
            ✅ GET /api/ returns {"message": "Hello World"}
            ✅ GET /api/installer/info returns metadata (size: 3670976 bytes, sha256: 39b473...)
            ✅ GET/HEAD /api/download/tricord-presence-installer.cia serves binary correctly
            ✅ POST /api/status creates status check with id and timestamp
            ✅ GET /api/status lists status checks
            
            NEW PHASE C ENDPOINTS (all working):
            ✅ POST /api/icons/{tid} with valid 4608 bytes → returns 200 with TID (uppercase), icon_url, PNG size (933 bytes)
            ✅ POST /api/icons/{tid} with wrong size (4000 bytes) → returns 400 with proper error
            ✅ POST /api/icons/{tid} with 15-char TID → returns 400 "Title ID must be 16 hex chars"
            ✅ POST /api/icons/{tid} with non-hex chars → returns 400 with proper error
            ✅ POST /api/icons/{tid} with lowercase TID → accepts and normalizes to uppercase
            ✅ POST /api/icons/{tid} idempotency → both uploads return 200
            ✅ GET /api/icons/{tid}.png → returns 200 with image/png for uploaded icons
            ✅ GET /api/icons/{tid}.png for non-existent → returns 404
            ✅ GET /api/titles/{tid} after icon upload → has icon_url set correctly
            ✅ GET /api/titles/{tid} for non-existent title → returns 200 with null fields
            ✅ POST /api/titles/{tid} → upserts title with name and source
            ✅ GET /api/installer/info mtime detection → recalculates metadata when file changes
            
            VERIFICATION:
            - PNG conversion working: 4608 bytes RGB565 → 192x192 PNG (933 bytes)
            - MongoDB persistence working (titles collection updated)
            - No errors in backend logs
            - All TID normalization working (uppercase, validation)
            - Icon files stored correctly in /app/backend/files/icons/

  - task: "Refresh CIA metadata après rebuild"
    implemented: true
    working: true
    file: "/app/backend/server.py"
    stuck_count: 0
    priority: "high"
    needs_retesting: false
    status_history:
        - working: true
          agent: "main"
          comment: |
            _get_cia_meta() vérifie mtime avant de renvoyer la meta cachée.
            Le nouveau .cia (3.67 MB, sha 39b473...) est bien renvoyé par
            /api/installer/info et téléchargeable via /api/download/.

frontend:
  - task: "QR + méta CIA (existant, non modifié)"
    implemented: true
    working: true
    file: "/app/frontend/src/App.js"
    stuck_count: 0
    priority: "low"
    needs_retesting: false
    status_history:
        - working: true
          agent: "main"
          comment: |
            Screenshot confirme : QR code régénéré avec la nouvelle URL, version v2,
            taille 3.50 Mo, sha256 39b473519e7103f2... affichée correctement.

  - task: "Sysmodule : lecture SMDH + Rich Presence enrichie (natif C)"
    implemented: true
    working: "NA"
    file: "/app/tricord_src/tricord-presence/sysmodule/source/"
    stuck_count: 0
    priority: "high"
    needs_retesting: true
    status_history:
        - working: "NA"
          agent: "main"
          comment: |
            Ajouts C (compilés OK avec devkitARM 16.1.0) :
             1. smdh_reader.c/h : ouvre l'archive SavedataAndContent du titre
                courant via FSUSER_OpenFileDirectly, lit le fichier ExeFS "icon",
                parse le SMDH (magic + AppTitle[16] UTF-16-LE), extrait le nom
                localisé (ordre EN>FR>JA>DE>IT>ES...) et la grande icône
                48×48 RGB565 (offset 0x24C0).
             2. presence_state.h : ajout de started_at_ms, media_type,
                has_icon + buffer icon_rgb565[4608]. Le protocole IPC vers le
                plugin overlay reste inchangé (16 words game_name).
             3. apt_monitor.c : appelle smdhReaderExtract quand le titre change,
                fallback titles.txt, ajoute timestamp Unix synthétique via
                svcGetSystemTick + epoch fixe 2024-01-01 (RTC 3DS non fiable).
             4. discord_gateway.c :
                  - loadConfigExtras() lit application_id + backend_base_url
                    depuis sdmc:/3ds/tricord-presence/config.txt (avec défauts
                    embarqués : app 1546529994421829693 + backend preview).
                  - build_activity_in_game() envoie name, type=0, details=<nom>,
                    state="Nintendo 3DS", application_id, timestamps.start pour
                    le chrono live Discord, et assets.large_image en format
                    mp:external/https/<host>/api/icons/<TID>.png (uniquement si
                    l'upload a réussi pour ce TID).
                  - httpsPostBinary() : POST HTTPS one-shot standalone
                    (mbedtls + bundle CA, tolérance RTC identique à la Gateway)
                    qui envoie les 4608 octets d'icône au backend.
                  - tryUploadIcon() : appelé avant chaque send_presence_update,
                    idempotent (s_iconTIDUploaded tracked par TID).
             5. Exclusions installeur/sysmodule intégrées (commit précédent
                4ad763b) restent en place.
            Livrable : dist/tricord-presence-installer.cia (3.67 MB, sha256
            39b473...) copié dans /app/backend/files/ — installable via FBI QR.

  - task: "Titles DB étendue (gen_titles_db.py + titles_extra.txt)"
    implemented: true
    working: true
    file: "/app/tricord_src/tricord-presence/tools/"
    stuck_count: 0
    priority: "medium"
    needs_retesting: false
    status_history:
        - working: true
          agent: "main"
          comment: |
            gen_titles_db.py : ajoute la catégorie 00040010 (system apps) aux
            00040000/00040002 déjà présentes ; supporte un fichier
            titles_extra.txt à côté du script (ou du fichier de sortie) qui
            écrase les entrées 3dsdb en cas de doublon. Utile pour homebrew
            (FBI, GodMode9, Anemone3DS...) et TIDs custom absents de 3dsdb.
            Vérifié : "4226 titres → titles.txt" (vs 4205 avant, +21 entrées
            manuelles), fichier bien embarqué dans le .cia via installer/romfs/.

metadata:
  created_by: "main_agent"
  version: "1.0"
  test_sequence: 2
  run_ui: false

test_plan:
  current_focus: []
  stuck_tasks: []
  test_all: false
  test_priority: "high_first"

agent_communication:
    - agent: "main"
      message: |
        Ajout Rich Presence enrichie (b + c + d) :
         - Backend : nouveaux endpoints icônes/titres avec conversion
           SMDH RGB565 → PNG (ordre Z, Pillow), cache Mongo, cache CIA
           auto-invalidé sur rebuild.
         - Sysmodule : lecture SMDH on-device (nom localisé + icône 48x48),
           Rich Presence enrichie (application_id 1546529994421829693,
           details, state, timestamps.start, assets.large_image via
           mp:external), HTTPS POST icône vers backend avec mbedtls.
         - titles.txt : catégorie 00040010 ajoutée + support titles_extra.txt.
         - Nouveau .cia (3.67 MB) livré dans /app/backend/files/.
        À tester : les 3 endpoints backend (POST/GET icons, GET/POST titles,
        installer/info avec refresh mtime). L'exécution sur 3DS réelle
        (SMDH read + icon POST) ne peut pas être testée automatiquement.
    - agent: "testing"
      message: |
        Backend testing completed successfully - ALL 17 TESTS PASSED.
        
        Tested and verified:
        ✅ All existing endpoints (root, installer/info, download, status) working
        ✅ All Phase C endpoints (icons upload/retrieval, titles CRUD) working
        ✅ TID validation and normalization (16 hex chars, uppercase conversion)
        ✅ Error handling (400 for invalid TID, wrong size, 404 for non-existent)
        ✅ Idempotency (duplicate uploads return 200)
        ✅ PNG conversion (4608 bytes RGB565 → 192x192 PNG)
        ✅ MongoDB persistence (titles collection updated correctly)
        ✅ CIA metadata mtime detection (recalculates on file change)
        
        No errors in backend logs. All backend functionality ready for production.
        The sysmodule integration (SMDH reading + icon POST from 3DS) cannot be
        tested automatically but the backend endpoints are fully functional.