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
