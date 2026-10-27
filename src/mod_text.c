/* See mod_text.h. Order in every row: jpn eng fre ita ger spa oth(Spanish,
 * Latin America) kor pol por rus chT chS ara.
 *
 * ⚠ Rows below MT_STAGE_PREFIX are the mod's own translations, written
 * 2026-10-15 without a native check in any language but English. Where the
 * collection has a word for a concept, these follow it (its "Stage:" is
 * Livello / Arena / Fase / Арена / 擂台 / 关卡 / المرحلة, so "Random stage"
 * does too). A player's correction replaces a cell here; nothing else
 * depends on the wording. */
#include "mod_text.h"
#include "mod_text_gmd.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#define MT_GMD_ROW(id, ...) [id] = { __VA_ARGS__ },

static const wchar_t *const g_text[MT_COUNT][MT_NLANG] = {
    MT_GMD_ROWS

    [MT_POS] = { L"{1}中{0}", L"{0} of {1}", L"{0} sur {1}", L"{0} di {1}", L"{0} von {1}",
                 L"{0} de {1}", L"{0} de {1}", L"{1}개 중 {0}", L"{0} z {1}", L"{0} de {1}",
                 L"{0} из {1}", L"第{0}項，共{1}項", L"第{0}项，共{1}项", L"{0} من {1}" },
    [MT_ROW_N] = { L"{0}行目", L"row {0}", L"ligne {0}", L"riga {0}", L"Zeile {0}",
                   L"fila {0}", L"fila {0}", L"{0}번째 행", L"wiersz {0}", L"linha {0}",
                   L"строка {0}", L"第{0}行", L"第{0}行", L"الصف {0}" },
    [MT_LEVEL] = { L"{0}（最大{1}）", L"{0}, of {1}", L"{0}, sur {1}", L"{0}, su {1}", L"{0}, von {1}",
                   L"{0}, de {1}", L"{0}, de {1}", L"{0}, 최대 {1}", L"{0}, z {1}", L"{0}, de {1}",
                   L"{0}, из {1}", L"{0}，最高{1}", L"{0}，最高{1}", L"{0}، من {1}" },
    [MT_OR] = { L"{0}または{1}", L"{0} or {1}", L"{0} ou {1}", L"{0} o {1}", L"{0} oder {1}",
                L"{0} o {1}", L"{0} o {1}", L"{0} 또는 {1}", L"{0} lub {1}", L"{0} ou {1}",
                L"{0} или {1}", L"{0}或{1}", L"{0}或{1}", L"{0} أو {1}" },

    [MT_RANDOM_STAGE] = { L"ランダムステージ", L"Random stage", L"Stage aléatoire", L"Livello casuale",
                          L"Zufällige Stage", L"Escenario aleatorio", L"Escenario aleatorio",
                          L"랜덤 스테이지", L"Losowa arena", L"Fase aleatória", L"Случайная арена",
                          L"隨機擂台", L"随机关卡", L"مرحلة عشوائية" },
    [MT_TEAM_RATIO] = { L"チームレシオ{0}（最大{1}）", L"Team ratio {0} of {1}", L"Ratio d'équipe {0} sur {1}",
                        L"Ratio della squadra {0} su {1}", L"Team-Ratio {0} von {1}",
                        L"Ratio de equipo {0} de {1}", L"Ratio de equipo {0} de {1}",
                        L"팀 레시오 {0}, 최대 {1}", L"Ratio drużyny {0} z {1}", L"Ratio da equipe {0} de {1}",
                        L"Ratio команды {0} из {1}", L"隊伍Ratio {0}，共{1}", L"队伍Ratio {0}，共{1}",
                        L"نسبة الفريق {0} من {1}" },
    [MT_RATIO] = { L"レシオ{0}（最大{1}）", L"Ratio {0} of {1}", L"Ratio {0} sur {1}", L"Ratio {0} su {1}",
                   L"Ratio {0} von {1}", L"Ratio {0} de {1}", L"Ratio {0} de {1}", L"레시오 {0}, 최대 {1}",
                   L"Ratio {0} z {1}", L"Ratio {0} de {1}", L"Ratio {0} из {1}", L"Ratio {0}，共{1}",
                   L"Ratio {0}，共{1}", L"النسبة {0} من {1}" },
    [MT_MEMBER_RATIO] = { L"{0}、レシオ{1}", L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}",
                          L"{0}, Ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}, 레시오 {1}",
                          L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}，Ratio {1}",
                          L"{0}，Ratio {1}", L"{0}، النسبة {1}" },
    [MT_MEMBER_RATIO_LIST] = { L"{0}、レシオ{1}", L"{0} ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}",
                               L"{0}, Ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}, 레시오 {1}",
                               L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}, ratio {1}", L"{0}，Ratio {1}",
                               L"{0}，Ratio {1}", L"{0}، النسبة {1}" },
    [MT_RATIOS] = { L"レシオ：", L"Ratios: ", L"Ratios : ", L"Ratio: ", L"Ratios: ", L"Ratios: ",
                    L"Ratios: ", L"레시오: ", L"Ratio: ", L"Ratios: ", L"Ratio: ", L"Ratio：",
                    L"Ratio：", L"النسب: " },
    [MT_LEVEL_N] = { L"レベル{0}", L"Level {0}", L"Niveau {0}", L"Livello {0}", L"Stufe {0}",
                     L"Nivel {0}", L"Nivel {0}", L"레벨 {0}", L"Poziom {0}", L"Nível {0}",
                     L"Уровень {0}", L"等級{0}", L"等级{0}", L"المستوى {0}" },
    [MT_MODE] = { L"{0}モード", L"{0} mode", L"Mode {0}", L"Modalità {0}", L"{0}-Modus",
                  L"Modo {0}", L"Modo {0}", L"{0} 모드", L"Tryb {0}", L"Modo {0}",
                  L"Режим {0}", L"{0}模式", L"{0}模式", L"وضع {0}" },
    [MT_MODE_NORMAL] = { L"ノーマル", L"Normal", L"normal", L"normale", L"Normal", L"normal",
                         L"normal", L"노멀", L"normalny", L"normal", L"обычный", L"普通",
                         L"普通", L"عادي" },
    [MT_MODE_MAZI] = { L"マジ", L"Mazi", L"Mazi", L"Mazi", L"Mazi", L"Mazi", L"Mazi", L"마지",
                       L"Mazi", L"Mazi", L"Мадзи", L"Mazi", L"Mazi", L"مازي" },
    [MT_MODE_SAIKYO] = { L"サイキョー", L"Saikyo", L"Saikyo", L"Saikyo", L"Saikyo", L"Saikyo",
                         L"Saikyo", L"사이쿄", L"Saikyo", L"Saikyo", L"Сайкё", L"Saikyo",
                         L"Saikyo", L"سايكيو" },
    [MT_MODE_CLASSICAL] = { L"クラシカル", L"Classical", L"classique", L"classica", L"Klassik",
                            L"clásico", L"clásico", L"클래시컬", L"klasyczny", L"clássico",
                            L"классический", L"經典", L"经典", L"كلاسيكي" },
    [MT_ORDER] = { L"{0}、{1}", L"{0} {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}",
                   L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}，{1}",
                   L"{0}，{1}", L"{0}، {1}" },
    [MT_FIRST] = { L"1番手", L"first", L"premier", L"primo", L"erster", L"primero", L"primero",
                   L"첫 번째", L"pierwszy", L"primeiro", L"первый", L"第一", L"第一", L"الأول" },
    [MT_SECOND] = { L"2番手", L"second", L"deuxième", L"secondo", L"zweiter", L"segundo", L"segundo",
                    L"두 번째", L"drugi", L"segundo", L"второй", L"第二", L"第二", L"الثاني" },
    [MT_THIRD] = { L"3番手", L"third", L"troisième", L"terzo", L"dritter", L"tercero", L"tercero",
                   L"세 번째", L"trzeci", L"terceiro", L"третий", L"第三", L"第三", L"الثالث" },
    [MT_FOURTH] = { L"4番手", L"fourth", L"quatrième", L"quarto", L"vierter", L"cuarto", L"cuarto",
                    L"네 번째", L"czwarty", L"quarto", L"четвёртый", L"第四", L"第四", L"الرابع" },
    [MT_YOU] = { L"あなた", L"You", L"Vous", L"Tu", L"Du", L"Tú", L"Tú", L"나", L"Ty", L"Você",
                 L"Вы", L"你", L"你", L"أنت" },
    /* Project Justice's first menu. "Story mode" as the collection words it
     * (ACHIEVEMENT_32_DESC in each language); "Free mode" as SOUND_MJUS_40. */
    [MT_STORY_MODE] = { L"ストーリーモード", L"Story mode", L"Mode histoire", L"Modalità Storia",
                        L"Story-Modus", L"Modo historia", L"Modo historia", L"스토리 모드",
                        L"Tryb historii", L"Modo história", L"Режим истории", L"故事模式",
                        L"故事模式", L"وضع القصة" },
    [MT_FREE_MODE] = { L"フリーモード", L"Free mode", L"Mode libre", L"Modalità libera",
                       L"Freier Modus", L"Modo libre", L"Modo libre", L"프리 모드",
                       L"Tryb dowolny", L"Modo livre", L"Свободный режим", L"自由模式",
                       L"自由模式", L"الوضع الحر" },

    [MT_CUE_HEALTH] = { L"体力", L"health", L"santé", L"salute", L"Lebensenergie", L"salud", L"salud",
                        L"체력", L"zdrowie", L"vida", L"здоровье", L"體力", L"体力", L"الصحة" },
    [MT_CUE_SUPER] = { L"スーパーゲージ", L"super meter", L"jauge de super", L"barra super",
                       L"Super-Leiste", L"barra de súper", L"barra de súper", L"슈퍼 게이지",
                       L"pasek super", L"barra de super", L"шкала супера", L"超必殺量表",
                       L"超必杀槽", L"مقياس الضربة الخارقة" },
    [MT_CUE_BEACONS] = { L"ビーコン", L"beacons", L"balises", L"segnali", L"Signale", L"balizas",
                         L"balizas", L"비컨", L"sygnały", L"sinalizadores", L"маяки", L"信標",
                         L"信标", L"المنارات" },
    [MT_CUE_TICK] = { L"時計の音", L"clock tick", L"tic de l'horloge", L"ticchettio del timer",
                      L"Uhr-Ticken", L"tic del reloj", L"tic del reloj", L"시계 소리", L"tykanie zegara",
                      L"tique do relógio", L"тиканье часов", L"計時聲", L"计时声", L"دقات الساعة" },
    [MT_MUTED] = { L"{0} ミュート", L"{0} muted", L"{0} coupé", L"{0} disattivato", L"{0} stumm",
                   L"{0} silenciado", L"{0} silenciado", L"{0} 음소거", L"{0} wyciszone",
                   L"{0} sem som", L"{0}: без звука", L"{0} 靜音", L"{0} 静音", L"{0} مكتوم" },
    [MT_PERCENT] = { L"{0} {1}パーセント", L"{0} {1} percent", L"{0} {1} pour cent", L"{0} {1} percento",
                     L"{0} {1} Prozent", L"{0} {1} por ciento", L"{0} {1} por ciento", L"{0} {1}퍼센트",
                     L"{0} {1} procent", L"{0} {1} por cento", L"{0}: {1} процентов",
                     L"{0} 百分之{1}", L"{0} 百分之{1}", L"{0} {1} بالمئة" },

    /* Power Stone pickups: who, then what kind (the game's item table names
     * no items as text, so the kind is all the mod can say). */
    [MT_PICKUP] = { L"{0}、{1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}",
                    L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}, {1}", L"{0}，{1}",
                    L"{0}，{1}", L"{0}، {1}" },
    [MT_PICK_STONE] = { L"パワーストーン", L"power stone", L"pierre de pouvoir", L"pietra del potere",
                        L"Power Stone", L"piedra de poder", L"piedra de poder", L"파워 스톤",
                        L"kamień mocy", L"pedra do poder", L"камень силы", L"力量石", L"力量石",
                        L"حجر القوة" },
    [MT_PICK_FOOD] = { L"食べ物", L"food", L"nourriture", L"cibo", L"Essen", L"comida", L"comida",
                       L"음식", L"jedzenie", L"comida", L"еда", L"食物", L"食物", L"طعام" },
    [MT_PICK_ITEM] = { L"アイテム", L"item", L"objet", L"oggetto", L"Gegenstand", L"objeto", L"objeto",
                       L"아이템", L"przedmiot", L"item", L"предмет", L"道具", L"道具", L"عنصر" },
    [MT_PICK_TREASURE] = { L"宝物", L"treasure", L"trésor", L"tesoro", L"Schatz", L"tesoro", L"tesoro",
                           L"보물", L"skarb", L"tesouro", L"сокровище", L"寶物", L"宝物", L"كنز" },
    [MT_PICK_THROWABLE] = { L"投げられる物", L"something to throw", L"objet à lancer",
                            L"oggetto da lanciare", L"Wurfobjekt", L"objeto para lanzar",
                            L"objeto para lanzar", L"던질 물건", L"przedmiot do rzucania",
                            L"objeto para arremessar", L"предмет для броска", L"可投擲物",
                            L"可投掷物", L"شيء للرمي" },
    [MT_PICKUPS_ALL] = { L"拾得の読み上げ、全員", L"Pickup announcements, every fighter",
                         L"Annonces des objets ramassés, tous les combattants",
                         L"Annunci delle raccolte, tutti i lottatori",
                         L"Aufsammel-Ansagen, alle Kämpfer", L"Avisos de recogida, todos los luchadores",
                         L"Avisos de recogida, todos los luchadores", L"획득 알림, 모든 파이터",
                         L"Komunikaty o podniesieniu, wszyscy wojownicy",
                         L"Avisos de coleta, todos os lutadores", L"Объявления о подборе, все бойцы",
                         L"拾取播報，所有角色", L"拾取播报，所有角色", L"إعلانات الالتقاط، كل المقاتلين" },
    [MT_PICKUPS_PEOPLE] = { L"拾得の読み上げ、プレイヤーのみ", L"Pickup announcements, players only",
                            L"Annonces des objets ramassés, joueurs seulement",
                            L"Annunci delle raccolte, solo giocatori",
                            L"Aufsammel-Ansagen, nur Spieler", L"Avisos de recogida, solo jugadores",
                            L"Avisos de recogida, solo jugadores", L"획득 알림, 플레이어만",
                            L"Komunikaty o podniesieniu, tylko gracze",
                            L"Avisos de coleta, só jogadores", L"Объявления о подборе, только игроки",
                            L"拾取播報，僅限玩家", L"拾取播报，仅限玩家", L"إعلانات الالتقاط، اللاعبون فقط" },
    [MT_PICKUPS_OFF] = { L"拾得の読み上げ、オフ", L"Pickup announcements off",
                         L"Annonces des objets ramassés désactivées", L"Annunci delle raccolte disattivati",
                         L"Aufsammel-Ansagen aus", L"Avisos de recogida desactivados",
                         L"Avisos de recogida desactivados", L"획득 알림 끔",
                         L"Komunikaty o podniesieniu wyłączone", L"Avisos de coleta desligados",
                         L"Объявления о подборе выключены", L"拾取播報關閉", L"拾取播报关闭",
                         L"إعلانات الالتقاط متوقفة" },
    [MT_OFFSCREEN_ON] = { L"画面外の音、オン", L"Off-screen sounds on", L"Sons hors écran activés",
                          L"Suoni fuori schermo attivi", L"Geräusche außerhalb des Bildes an",
                          L"Sonidos fuera de pantalla activados", L"Sonidos fuera de pantalla activados",
                          L"화면 밖 소리 켬", L"Dźwięki spoza ekranu włączone",
                          L"Sons fora da tela ligados", L"Звуки за экраном включены",
                          L"畫面外聲音開啟", L"画面外声音开启", L"أصوات خارج الشاشة مفعلة" },
    /* Power Stone 2's stage select: walk to one of five stages. Directions
     * are from the player's fighter, as the camera faces. */
    [MT_STAGE_SELECT] = { L"ステージ選択", L"Stage select", L"Choix du stage", L"Selezione livello",
                          L"Stage-Auswahl", L"Selección de escenario", L"Selección de escenario",
                          L"스테이지 선택", L"Wybór areny", L"Seleção de fase", L"Выбор арены",
                          L"選擇關卡", L"选择关卡", L"اختيار المرحلة" },
    [MT_DIR_AHEAD] = { L"前", L"ahead", L"devant", L"avanti", L"vorne", L"adelante", L"adelante",
                       L"앞", L"przed tobą", L"à frente", L"впереди", L"前方", L"前方", L"أمامك" },
    [MT_DIR_AHEAD_RIGHT] = { L"右前", L"ahead right", L"devant à droite", L"avanti a destra",
                             L"vorne rechts", L"adelante a la derecha", L"adelante a la derecha",
                             L"오른쪽 앞", L"z przodu po prawej", L"à frente à direita",
                             L"впереди справа", L"右前方", L"右前方", L"أمامك يمينًا" },
    [MT_DIR_RIGHT] = { L"右", L"right", L"à droite", L"a destra", L"rechts", L"a la derecha",
                       L"a la derecha", L"오른쪽", L"po prawej", L"à direita", L"справа", L"右邊",
                       L"右边", L"يمينك" },
    [MT_DIR_BEHIND_RIGHT] = { L"右後ろ", L"behind right", L"derrière à droite", L"dietro a destra",
                              L"hinten rechts", L"detrás a la derecha", L"detrás a la derecha",
                              L"오른쪽 뒤", L"z tyłu po prawej", L"atrás à direita",
                              L"сзади справа", L"右後方", L"右后方", L"خلفك يمينًا" },
    [MT_DIR_BEHIND] = { L"後ろ", L"behind", L"derrière", L"dietro", L"hinten", L"detrás", L"detrás",
                        L"뒤", L"za tobą", L"atrás", L"сзади", L"後方", L"后方", L"خلفك" },
    [MT_DIR_BEHIND_LEFT] = { L"左後ろ", L"behind left", L"derrière à gauche", L"dietro a sinistra",
                             L"hinten links", L"detrás a la izquierda", L"detrás a la izquierda",
                             L"왼쪽 뒤", L"z tyłu po lewej", L"atrás à esquerda",
                             L"сзади слева", L"左後方", L"左后方", L"خلفك يسارًا" },
    [MT_DIR_LEFT] = { L"左", L"left", L"à gauche", L"a sinistra", L"links", L"a la izquierda",
                      L"a la izquierda", L"왼쪽", L"po lewej", L"à esquerda", L"слева", L"左邊",
                      L"左边", L"يسارك" },
    [MT_DIR_AHEAD_LEFT] = { L"左前", L"ahead left", L"devant à gauche", L"avanti a sinistra",
                            L"vorne links", L"adelante a la izquierda", L"adelante a la izquierda",
                            L"왼쪽 앞", L"z przodu po lewej", L"à frente à esquerda",
                            L"впереди слева", L"左前方", L"左前方", L"أمامك يسارًا" },
    [MT_OFFSCREEN_OFF] = { L"画面外の音、オフ", L"Off-screen sounds off", L"Sons hors écran désactivés",
                           L"Suoni fuori schermo disattivati", L"Geräusche außerhalb des Bildes aus",
                           L"Sonidos fuera de pantalla desactivados", L"Sonidos fuera de pantalla desactivados",
                           L"화면 밖 소리 끔", L"Dźwięki spoza ekranu wyłączone",
                           L"Sons fora da tela desligados", L"Звуки за экраном выключены",
                           L"畫面外聲音關閉", L"画面外声音关闭", L"أصوات خارج الشاشة متوقفة" },

    [MT_READY] = { L"アクセスMOD準備完了。バックエンド {0}。", L"Access mod ready. Backend {0}.",
                   L"Mod d'accessibilité prêt. Moteur {0}.", L"Mod di accessibilità pronta. Motore {0}.",
                   L"Barrierefreiheits-Mod bereit. Backend {0}.", L"Mod de accesibilidad listo. Motor {0}.",
                   L"Mod de accesibilidad listo. Motor {0}.", L"접근성 모드 준비 완료. 백엔드 {0}.",
                   L"Mod dostępności gotowy. Silnik {0}.", L"Mod de acessibilidade pronto. Motor {0}.",
                   L"Мод доступности готов. Движок {0}.", L"無障礙模組已就緒。後端 {0}。",
                   L"无障碍模组已就绪。后端 {0}。", L"تعديل الوصول جاهز. المحرك {0}." },
    [MT_NOT_UNPACKED] = {
        L"アクセスMODを読み込みましたが、ゲームの展開が完了しませんでした。バックエンド {0}。メニューは読み上げられません。",
        L"Access mod loaded, backend {0}, but the image never unpacked. Menu reading will not work.",
        L"Mod d'accessibilité chargé, moteur {0}, mais le jeu ne s'est jamais décompressé. La lecture des menus ne fonctionnera pas.",
        L"Mod di accessibilità caricata, motore {0}, ma il gioco non si è mai decompresso. La lettura dei menu non funzionerà.",
        L"Barrierefreiheits-Mod geladen, Backend {0}, aber das Spiel wurde nie entpackt. Menüs werden nicht vorgelesen.",
        L"Mod de accesibilidad cargado, motor {0}, pero el juego nunca se descomprimió. Los menús no se leerán.",
        L"Mod de accesibilidad cargado, motor {0}, pero el juego nunca se descomprimió. Los menús no se leerán.",
        L"접근성 모드를 불러왔지만 게임 압축이 풀리지 않았습니다. 백엔드 {0}. 메뉴를 읽을 수 없습니다.",
        L"Mod dostępności wczytany, silnik {0}, ale gra nigdy się nie rozpakowała. Menu nie będą czytane.",
        L"Mod de acessibilidade carregado, motor {0}, mas o jogo nunca foi descompactado. Os menus não serão lidos.",
        L"Мод доступности загружен, движок {0}, но игра так и не распаковалась. Меню не будут читаться.",
        L"無障礙模組已載入，後端 {0}，但遊戲始終未解壓。選單無法朗讀。",
        L"无障碍模组已加载，后端 {0}，但游戏始终未解压。菜单无法朗读。",
        L"تم تحميل تعديل الوصول، المحرك {0}، لكن اللعبة لم تُفك أبدًا. لن تُقرأ القوائم." },
    [MT_NONE] = { L"なし", L"none", L"aucun", L"nessuno", L"keins", L"ninguno", L"ninguno", L"없음",
                  L"brak", L"nenhum", L"нет", L"無", L"无", L"لا شيء" },

    [MT_TEXT_NO_READER] = { L"テキストの読み取り機能がありません。", L"No text reader is available.",
                            L"Aucun lecteur de texte disponible.", L"Nessun lettore di testo disponibile.",
                            L"Kein Textleser verfügbar.", L"No hay lector de texto disponible.",
                            L"No hay lector de texto disponible.", L"사용할 수 있는 텍스트 리더가 없습니다.",
                            L"Brak dostępnego czytnika tekstu.", L"Nenhum leitor de texto disponível.",
                            L"Нет доступного чтения текста.", L"沒有可用的文字讀取。",
                            L"没有可用的文字读取。", L"لا يوجد قارئ نصوص متاح." },
    [MT_TEXT_NOTHING] = { L"画面に読み取れるテキストがありません。", L"No readable text on screen.",
                          L"Aucun texte lisible à l'écran.", L"Nessun testo leggibile sullo schermo.",
                          L"Kein lesbarer Text auf dem Bildschirm.", L"No hay texto legible en pantalla.",
                          L"No hay texto legible en pantalla.", L"화면에 읽을 수 있는 텍스트가 없습니다.",
                          L"Brak czytelnego tekstu na ekranie.", L"Nenhum texto legível na tela.",
                          L"На экране нет читаемого текста.", L"畫面上沒有可讀的文字。",
                          L"屏幕上没有可读的文字。", L"لا يوجد نص مقروء على الشاشة." },
    [MT_TEXT_STALE] = { L"新しいテキストがまだありません。", L"No fresh text frame is available.",
                        L"Aucun texte récent disponible.", L"Nessun testo recente disponibile.",
                        L"Kein aktueller Text verfügbar.", L"No hay texto reciente disponible.",
                        L"No hay texto reciente disponible.", L"새 텍스트가 아직 없습니다.",
                        L"Brak aktualnego tekstu.", L"Nenhum texto recente disponível.",
                        L"Свежего текста пока нет.", L"尚無最新文字。", L"尚无最新文字。",
                        L"لا يوجد نص حديث متاح." },
    [MT_TEXT_INACTIVE] = { L"ここではテキストを読み取れません。", L"No text reader is active here.",
                           L"Aucun lecteur de texte actif ici.", L"Nessun lettore di testo attivo qui.",
                           L"Hier ist kein Textleser aktiv.", L"Ningún lector de texto activo aquí.",
                           L"Ningún lector de texto activo aquí.", L"여기서는 텍스트 리더가 작동하지 않습니다.",
                           L"Żaden czytnik tekstu nie działa tutaj.", L"Nenhum leitor de texto ativo aqui.",
                           L"Здесь чтение текста не работает.", L"此處沒有運作中的文字讀取。",
                           L"此处没有运行中的文字读取。", L"لا يعمل قارئ النصوص هنا." },
};

static const char *const g_codes[MT_NLANG] = MT_LANGS;
static volatile LONG g_lang = -1;   /* -1 unknown: English */

int mt_language_index(const char *code)
{
    if (!code) return -1;
    for (int i = 0; i < MT_NLANG; i++)
        if (!strcmp(code, g_codes[i])) return i;
    return -1;
}

void mt_set_language(const char *code)
{
    InterlockedExchange(&g_lang, mt_language_index(code));
}

int mt_language(void)
{
    return (int)InterlockedCompareExchange(&g_lang, 0, 0);
}

void mt_update_language(const char *code)
{
    /* An unreadable or unknown language keeps the last good one; it never
     * flips the mod back to English mid-session. */
    int idx = mt_language_index(code);
    if (idx >= 0) InterlockedExchange(&g_lang, idx);
}

const wchar_t *mt_in(mt_id id, int lang)
{
    const wchar_t *s;
    if ((unsigned)id >= MT_COUNT) return L"";
    if (lang < 0 || lang >= MT_NLANG) lang = MT_ENGLISH;
    s = g_text[id][lang];
    return s && s[0] ? s : (g_text[id][MT_ENGLISH] ? g_text[id][MT_ENGLISH] : L"");
}

const wchar_t *mt(mt_id id) { return mt_in(id, mt_language()); }

void mt_fmt(wchar_t *out, size_t cap, mt_id id,
            const wchar_t *a0, const wchar_t *a1, const wchar_t *a2)
{
    const wchar_t *t = mt(id), *args[3] = { a0, a1, a2 };
    size_t n = 0;
    if (!out || !cap) return;
    while (*t && n + 1 < cap) {
        if (t[0] == L'{' && t[1] >= L'0' && t[1] <= L'2' && t[2] == L'}') {
            const wchar_t *a = args[t[1] - L'0'];
            while (a && *a && n + 1 < cap) out[n++] = *a++;
            t += 3;
        } else {
            out[n++] = *t++;
        }
    }
    out[n] = 0;
}

void mt_fmt_n(wchar_t *out, size_t cap, mt_id id, int n0, int n1)
{
    wchar_t a[16], b[16];
    swprintf(a, 16, L"%d", n0);
    swprintf(b, 16, L"%d", n1);
    mt_fmt(out, cap, id, a, b, NULL);
}

void mt_fmt8(char *out, size_t cap, mt_id id, const char *a0, const char *a1, const char *a2)
{
    wchar_t w[3][256], res[512];
    const char *in[3] = { a0, a1, a2 };
    if (!out || !cap) return;
    for (int i = 0; i < 3; i++) {
        w[i][0] = 0;
        if (in[i] && !MultiByteToWideChar(CP_UTF8, 0, in[i], -1, w[i], 256)) w[i][0] = 0;
    }
    mt_fmt(res, 512, id, w[0], w[1], w[2]);
    if (!WideCharToMultiByte(CP_UTF8, 0, res, -1, out, (int)cap, NULL, NULL)) out[0] = 0;
    out[cap - 1] = 0;
}
