#pragma once

// The dense-points step in the GUI: the dataset screen's section, the plan and
// log lines around it, the training screen's "Add Dense Points" row and its
// model chooser (app/gui/DensifyRunner.h, app/gui/RecomputePanel.h). What the
// child `spirula densify` prints is its own catalog (Densify.h). Folder names
// such as sparse/0-roma, the preset words and COLMAP stay as they are.

#include "i18n/BeginCatalog.h"

namespace spirula {
namespace i18n {
namespace msg {
namespace densegui {

SS_MSG(enable,
    EN("Add dense points (RoMa v2)"),
    JA("密な点を加える（RoMa v2）"),
    ZH_HANS("添加稠密点（RoMa v2）"),
    ZH_HANT("加入稠密點（RoMa v2）"),
    KO("조밀한 점 추가 (RoMa v2)"),
    DE("Dichte Punkte hinzufügen (RoMa v2)"),
    FR("Ajouter des points denses (RoMa v2)"),
    ES("Añadir puntos densos (RoMa v2)"),
    PT("Adicionar pontos densos (RoMa v2)"),
    IT("Aggiungi punti densi (RoMa v2)"),
    NL("Dichte punten toevoegen (RoMa v2)"),
    RU("Добавить плотные точки (RoMa v2)"),
    TR("Yoğun nokta ekle (RoMa v2)"));

SS_MSG(enable_help,
    EN("After the reconstruction, matches the images densely with RoMa v2 and triangulates the "
       "matches against the solved poses. The points go into a new model beside the sparse one "
       "(sparse/0-roma): its cameras are copied byte for byte and the sparse model is never "
       "written. Slow on a large capture. The first run downloads the RoMa v2 checkpoint, after "
       "you have read and accepted its licences."),
    JA("再構成のあと、RoMa v2 で画像を密にマッチングし、解いた姿勢に対してマッチを三角測量します。"
       "点はスパースなモデルの隣の新しいモデル（sparse/0-roma）に入り、カメラはバイト単位でそのまま"
       "写され、スパースなモデルには書き込みません。大きな撮影では時間がかかります。初回は、"
       "ライセンスを読んで同意したあとで RoMa v2 のチェックポイントをダウンロードします。"),
    ZH_HANS("重建完成后，用 RoMa v2 对图像做稠密匹配，并按已求解的位姿对匹配三角化。点写入稀疏模型旁的新模型"
            "（sparse/0-roma）：相机逐字节复制，稀疏模型从不写入。大型采集会很慢。首次运行会在你阅读并接受"
            "许可协议后下载 RoMa v2 检查点。"),
    ZH_HANT("重建完成後，用 RoMa v2 對影像做稠密匹配，並依已求解的位姿對匹配三角化。點寫入稀疏模型旁的新模型"
            "（sparse/0-roma）：相機逐位元組複製，稀疏模型從不寫入。大型擷取會很慢。首次執行會在你閱讀並接受"
            "授權條款後下載 RoMa v2 檢查點。"),
    KO("재구성이 끝나면 RoMa v2로 이미지를 조밀하게 매칭하고, 풀어 둔 포즈에 맞춰 매칭을 삼각측량합니다. "
       "점은 희소 모델 옆의 새 모델(sparse/0-roma)에 들어가며, 카메라는 바이트 그대로 복사되고 희소 모델에는 "
       "쓰지 않습니다. 큰 촬영본에서는 느립니다. 처음 실행할 때 라이선스를 읽고 동의한 뒤 RoMa v2 "
       "체크포인트를 내려받습니다."),
    DE("Gleicht die Bilder nach der Rekonstruktion dicht mit RoMa v2 ab und trianguliert die Treffer gegen die "
       "gelösten Posen. Die Punkte kommen in ein neues Modell neben dem dünnen (sparse/0-roma): Seine Kameras "
       "werden Byte für Byte kopiert, das dünne Modell wird nie geschrieben. Bei großen Aufnahmen langsam. Der "
       "erste Lauf lädt den RoMa-v2-Checkpoint herunter, nachdem Sie die Lizenzen gelesen und akzeptiert haben."),
    FR("Après la reconstruction, apparie les images de façon dense avec RoMa v2 et triangule les appariements sur "
       "les poses résolues. Les points vont dans un nouveau modèle à côté du modèle épars (sparse/0-roma) : ses "
       "caméras sont copiées octet pour octet et le modèle épars n'est jamais écrit. Lent sur une grosse capture. "
       "Le premier lancement télécharge le point de contrôle RoMa v2 une fois ses licences lues et acceptées."),
    ES("Tras la reconstrucción, empareja las imágenes de forma densa con RoMa v2 y triangula las correspondencias "
       "contra las poses resueltas. Los puntos van a un modelo nuevo junto al disperso (sparse/0-roma): sus cámaras "
       "se copian byte a byte y el modelo disperso nunca se escribe. Lento en una captura grande. La primera "
       "ejecución descarga el punto de control de RoMa v2 después de que leas y aceptes sus licencias."),
    PT("Depois da reconstrução, emparelha as imagens de forma densa com o RoMa v2 e triangula as correspondências "
       "contra as poses resolvidas. Os pontos vão para um modelo novo ao lado do esparso (sparse/0-roma): as suas "
       "câmaras são copiadas byte a byte e o modelo esparso nunca é escrito. Lento numa captura grande. A primeira "
       "execução descarrega o ponto de controlo do RoMa v2 depois de ler e aceitar as suas licenças."),
    IT("Dopo la ricostruzione, abbina le immagini in modo denso con RoMa v2 e triangola le corrispondenze sulle pose "
       "risolte. I punti vanno in un nuovo modello accanto a quello sparso (sparse/0-roma): le sue camere sono "
       "copiate byte per byte e il modello sparso non viene mai scritto. Lento su una cattura grande. La prima "
       "esecuzione scarica il checkpoint di RoMa v2 dopo che ne hai letto e accettato le licenze."),
    NL("Koppelt de beelden na de reconstructie dicht met RoMa v2 en trianguleert de koppelingen op de opgeloste "
       "poses. De punten komen in een nieuw model naast het ijle (sparse/0-roma): de camera's worden byte voor "
       "byte gekopieerd en het ijle model wordt nooit geschreven. Traag bij een grote opname. De eerste run "
       "downloadt het RoMa v2-checkpoint nadat u de licenties hebt gelezen en geaccepteerd."),
    RU("После реконструкции плотно сопоставляет снимки с помощью RoMa v2 и триангулирует соответствия по решённым "
       "позам. Точки попадают в новую модель рядом с разреженной (sparse/0-roma): её камеры копируются байт в байт, "
       "а разреженная модель никогда не записывается. На большой съёмке медленно. При первом запуске чекпойнт "
       "RoMa v2 скачивается после того, как вы прочтёте и примете его лицензии."),
    TR("Yeniden yapılandırmadan sonra görüntüleri RoMa v2 ile yoğun biçimde eşleştirir ve eşleşmeleri çözülmüş "
       "pozlara göre üçgenler. Noktalar seyrek modelin yanındaki yeni bir modele (sparse/0-roma) yazılır: "
       "kameraları bayt bayt kopyalanır, seyrek modele hiç yazılmaz. Büyük çekimlerde yavaştır. İlk çalıştırma, "
       "lisansları okuyup kabul etmenizin ardından RoMa v2 kontrol noktasını indirir."));

SS_MSG(unavailable,
    EN("Dense points are not part of this build."),
    JA("このビルドには密な点の機能がありません。"),
    ZH_HANS("此版本不含稠密点功能。"),
    ZH_HANT("此版本不含稠密點功能。"),
    KO("이 빌드에는 조밀한 점 기능이 없습니다."),
    DE("Dichte Punkte sind nicht Teil dieses Builds."),
    FR("Les points denses ne font pas partie de cette version."),
    ES("Los puntos densos no forman parte de esta compilación."),
    PT("Os pontos densos não fazem parte desta compilação."),
    IT("I punti densi non fanno parte di questa build."),
    NL("Dichte punten maken geen deel uit van deze build."),
    RU("В этой сборке нет плотных точек."),
    TR("Yoğun noktalar bu derlemede yok."));

SS_MSG(source_model,
    EN("Source model"),
    JA("元のモデル"),
    ZH_HANS("源模型"),
    ZH_HANT("來源模型"),
    KO("원본 모델"),
    DE("Quellmodell"),
    FR("Modèle source"),
    ES("Modelo de origen"),
    PT("Modelo de origem"),
    IT("Modello di origine"),
    NL("Bronmodel"),
    RU("Исходная модель"),
    TR("Kaynak model"));

SS_MSG(model_auto,
    EN("Auto (the model with the most images)"),
    JA("自動（画像が最も多いモデル）"),
    ZH_HANS("自动（图像最多的模型）"),
    ZH_HANT("自動（影像最多的模型）"),
    KO("자동 (이미지가 가장 많은 모델)"),
    DE("Automatisch (das Modell mit den meisten Bildern)"),
    FR("Auto (le modèle avec le plus d'images)"),
    ES("Automático (el modelo con más imágenes)"),
    PT("Automático (o modelo com mais imagens)"),
    IT("Automatico (il modello con più immagini)"),
    NL("Automatisch (het model met de meeste beelden)"),
    RU("Авто (модель с наибольшим числом снимков)"),
    TR("Otomatik (en çok görüntüsü olan model)"));

SS_MSG(preset,
    EN("Matcher preset"),
    JA("マッチャーのプリセット"),
    ZH_HANS("匹配器预设"),
    ZH_HANT("匹配器預設"),
    KO("매처 프리셋"),
    DE("Matcher-Voreinstellung"),
    FR("Préréglage du matcher"),
    ES("Preajuste del emparejador"),
    PT("Predefinição do emparelhador"),
    IT("Preset del matcher"),
    NL("Matcher-voorinstelling"),
    RU("Пресет сопоставления"),
    TR("Eşleştirici ön ayarı"));

SS_MSG(preset_help,
    EN("auto runs the default, base. turbo (320), fast (512) and base (640) match at a single "
       "resolution; high (640, then 960) and precise (800, then 1280) need more memory and time."),
    JA("auto は既定の base で実行します。turbo (320)、fast (512)、base (640) は 1 つの解像度でマッチングし、high (640 の次に 960) と "
       "precise (800 の次に 1280) はメモリも時間も多く必要です。"),
    ZH_HANS("auto 使用默认的 base。turbo (320)、fast (512) 和 base (640) 以单一分辨率匹配；high（先 640 后 960）和 precise（先 800 后 "
            "1280）需要更多内存和时间。"),
    ZH_HANT("auto 使用預設的 base。turbo (320)、fast (512) 和 base (640) 以單一解析度匹配；high（先 640 後 960）和 precise（先 800 後 "
            "1280）需要更多記憶體和時間。"),
    KO("auto는 기본값인 base로 실행합니다. turbo (320), fast (512), base (640)는 한 가지 해상도로 매칭하고, high (640 다음 960)와 "
       "precise (800 다음 1280)는 메모리와 시간이 더 필요합니다."),
    DE("auto verwendet den Standard base. turbo (320), fast (512) und base (640) gleichen in einer "
       "Auflösung ab; high (640, dann 960) und precise (800, dann 1280) brauchen mehr Speicher und Zeit."),
    FR("auto utilise la valeur par défaut, base. turbo (320), fast (512) et base (640) apparient à une "
       "seule résolution ; high (640 puis 960) et precise (800 puis 1280) demandent plus de mémoire et "
       "de temps."),
    ES("auto usa el valor por defecto, base. turbo (320), fast (512) y base (640) emparejan a una sola "
       "resolución; high (640 y luego 960) y precise (800 y luego 1280) necesitan más memoria y tiempo."),
    PT("auto usa o valor por omissão, base. turbo (320), fast (512) e base (640) emparelham numa só "
       "resolução; high (640 e depois 960) e precise (800 e depois 1280) precisam de mais memória e "
       "tempo."),
    IT("auto usa il valore predefinito, base. turbo (320), fast (512) e base (640) abbinano a una sola "
       "risoluzione; high (640 poi 960) e precise (800 poi 1280) richiedono più memoria e tempo."),
    NL("auto gebruikt de standaard, base. turbo (320), fast (512) en base (640) koppelen op één "
       "resolutie; high (640, dan 960) en precise (800, dan 1280) vragen meer geheugen en tijd."),
    RU("auto использует значение по умолчанию, base. turbo (320), fast (512) и base (640) сопоставляют в "
       "одном разрешении; high (640, затем 960) и precise (800, затем 1280) требуют больше памяти и "
       "времени."),
    TR("auto varsayılan olan base'i kullanır. turbo (320), fast (512) ve base (640) tek bir çözünürlükte "
       "eşleştirir; high (önce 640, sonra 960) ve precise (önce 800, sonra 1280) daha çok bellek ve "
       "zaman ister."));

SS_MSG(preset_missing,
    EN("This build's densify tool has no presets yet; it picks one itself."),
    JA("このビルドの densify にはまだプリセットがなく、自動で選びます。"),
    ZH_HANS("此版本的 densify 工具还没有预设，会自行选择。"),
    ZH_HANT("此版本的 densify 工具還沒有預設，會自行選擇。"),
    KO("이 빌드의 densify 도구에는 아직 프리셋이 없으며 스스로 고릅니다."),
    DE("Das Densify-Werkzeug dieses Builds hat noch keine Voreinstellungen; es wählt selbst."),
    FR("L'outil densify de cette version n'a pas encore de préréglages ; il en choisit un lui-même."),
    ES("La herramienta densify de esta compilación aún no tiene preajustes; elige uno por sí misma."),
    PT("A ferramenta densify desta compilação ainda não tem predefinições; escolhe uma sozinha."),
    IT("Lo strumento densify di questa build non ha ancora preset; ne sceglie uno da solo."),
    NL("De densify-tool van deze build heeft nog geen voorinstellingen; hij kiest zelf."),
    RU("В densify этой сборки пока нет пресетов; он выбирает сам."),
    TR("Bu derlemenin densify aracında henüz ön ayar yok; kendisi seçer."));

SS_MSG(source_from,
    EN("Points from"),
    JA("点の出所"),
    ZH_HANS("点的来源"),
    ZH_HANT("點的來源"),
    KO("점의 출처"),
    DE("Punkte aus"),
    FR("Origine des points"),
    ES("Puntos desde"),
    PT("Pontos a partir de"),
    IT("Punti da"),
    NL("Punten uit"),
    RU("Источник точек"),
    TR("Noktaların kaynağı"));

SS_MSG(source_auto,
    EN("Auto"),
    JA("自動"),
    ZH_HANS("自动"),
    ZH_HANT("自動"),
    KO("자동"),
    DE("Automatisch"),
    FR("Auto"),
    ES("Automático"),
    PT("Automático"),
    IT("Automatico"),
    NL("Automatisch"),
    RU("Авто"),
    TR("Otomatik"));

SS_MSG(source_roma,
    EN("RoMa matches"),
    JA("RoMa のマッチ"),
    ZH_HANS("RoMa 匹配"),
    ZH_HANT("RoMa 匹配"),
    KO("RoMa 매칭"),
    DE("RoMa-Zuordnungen"),
    FR("Appariements RoMa"),
    ES("Correspondencias RoMa"),
    PT("Correspondências RoMa"),
    IT("Corrispondenze RoMa"),
    NL("RoMa-overeenkomsten"),
    RU("Соответствия RoMa"),
    TR("RoMa eşleşmeleri"));

SS_MSG(source_moge,
    EN("MoGe depth"),
    JA("MoGe の深度"),
    ZH_HANS("MoGe 深度"),
    ZH_HANT("MoGe 深度"),
    KO("MoGe 깊이"),
    DE("MoGe-Tiefe"),
    FR("Profondeur MoGe"),
    ES("Profundidad MoGe"),
    PT("Profundidade MoGe"),
    IT("Profondità MoGe"),
    NL("MoGe-diepte"),
    RU("Глубина MoGe"),
    TR("MoGe derinliği"));

SS_MSG(source_hybrid,
    EN("Hybrid"),
    JA("ハイブリッド"),
    ZH_HANS("混合"),
    ZH_HANT("混合"),
    KO("하이브리드"),
    DE("Hybrid"),
    FR("Hybride"),
    ES("Híbrido"),
    PT("Híbrido"),
    IT("Ibrido"),
    NL("Hybride"),
    RU("Гибрид"),
    TR("Karma"));

SS_MSG(source_help,
    EN("Where the new points come from.\nAuto: RoMa matches (the depth maps only if RoMa cannot run); Hybrid only when chosen.\nRoMa matches: triangulated dense matches. Needs the RoMa v2 and DINOv3 licences and the checkpoint.\nMoGe depth: points from MoGe monocular depth maps fitted to the sparse model. Needs neither the RoMa checkpoint nor its licences; maps that are missing are made with spirula geometry.\nHybrid: matches first, depth fills what they miss. Needs the RoMa checkpoint."),
    JA("新しい点の出所です。\n自動: RoMa のマッチ (RoMa が動かないときだけ深度マップ)。ハイブリッドは選んだときだけ。\nRoMa のマッチ: 三角測量した密なマッチ。RoMa v2 と DINOv3 のライセンスとチェックポイントが必要です。\nMoGe の深度: 疎なモデルに合わせた MoGe の単眼深度マップから点を作ります。RoMa のチェックポイントもライセンスも不要で、無い深度マップは spirula geometry で作ります。\nハイブリッド: まずマッチ、取りこぼしを深度で補います。RoMa のチェックポイントが必要です。"),
    ZH_HANS("新点的来源。\n自动：RoMa 匹配（仅在 RoMa 无法运行时才用深度图）；混合须手动选择。\nRoMa 匹配：三角化的稠密匹配。需要 RoMa v2 与 DINOv3 的许可和检查点。\nMoGe 深度：由拟合到稀疏模型的 MoGe 单目深度图生成点。不需要 RoMa 检查点及其许可；缺少的深度图由 spirula geometry 生成。\n混合：先用匹配，深度补足其遗漏。需要 RoMa 检查点。"),
    ZH_HANT("新點的來源。\n自動：RoMa 匹配（僅在 RoMa 無法執行時才用深度圖）；混合須手動選擇。\nRoMa 匹配：三角化的稠密匹配。需要 RoMa v2 與 DINOv3 的授權和檢查點。\nMoGe 深度：由擬合到稀疏模型的 MoGe 單目深度圖產生點。不需要 RoMa 檢查點及其授權；缺少的深度圖由 spirula geometry 產生。\n混合：先用匹配，深度補足其遺漏。需要 RoMa 檢查點。"),
    KO("새 점의 출처입니다.\n자동: RoMa 매칭 (RoMa를 쓸 수 없을 때만 깊이 맵); 하이브리드는 직접 고를 때만.\nRoMa 매칭: 삼각측량한 조밀 매칭. RoMa v2와 DINOv3 라이선스와 체크포인트가 필요합니다.\nMoGe 깊이: 희소 모델에 맞춘 MoGe 단안 깊이 맵에서 점을 만듭니다. RoMa 체크포인트도 라이선스도 필요 없으며, 없는 깊이 맵은 spirula geometry로 만듭니다.\n하이브리드: 매칭을 먼저 쓰고 놓친 곳을 깊이로 채웁니다. RoMa 체크포인트가 필요합니다."),
    DE("Woher die neuen Punkte kommen.\nAutomatisch: RoMa-Zuordnungen (Tiefenkarten nur, wenn RoMa nicht laufen kann); Hybrid nur auf Wunsch.\nRoMa-Zuordnungen: triangulierte dichte Zuordnungen. Braucht die Lizenzen von RoMa v2 und DINOv3 und den Checkpoint.\nMoGe-Tiefe: Punkte aus monokularen MoGe-Tiefenkarten, die ans dünne Modell angepasst werden. Braucht weder den RoMa-Checkpoint noch dessen Lizenzen; fehlende Karten erzeugt spirula geometry.\nHybrid: erst Zuordnungen, die Tiefe füllt, was sie verpassen. Braucht den RoMa-Checkpoint."),
    FR("D'où viennent les nouveaux points.\nAuto : appariements RoMa (les cartes de profondeur seulement si RoMa ne peut pas tourner) ; hybride seulement si choisi.\nAppariements RoMa : appariements denses triangulés. Demande les licences de RoMa v2 et de DINOv3 et le point de contrôle.\nProfondeur MoGe : points issus des cartes de profondeur monoculaires MoGe ajustées au modèle épars. Ne demande ni le point de contrôle RoMa ni ses licences ; les cartes manquantes sont produites par spirula geometry.\nHybride : les appariements d'abord, la profondeur comble ce qu'ils manquent. Demande le point de contrôle RoMa."),
    ES("De dónde salen los puntos nuevos.\nAutomático: correspondencias RoMa (los mapas de profundidad solo si RoMa no puede ejecutarse); híbrido solo si se elige.\nCorrespondencias RoMa: correspondencias densas trianguladas. Requiere las licencias de RoMa v2 y DINOv3 y el punto de control.\nProfundidad MoGe: puntos de los mapas de profundidad monoculares de MoGe ajustados al modelo disperso. No requiere el punto de control de RoMa ni sus licencias; los mapas que faltan los crea spirula geometry.\nHíbrido: primero las correspondencias, la profundidad rellena lo que se les escapa. Requiere el punto de control de RoMa."),
    PT("De onde vêm os novos pontos.\nAutomático: correspondências RoMa (os mapas de profundidade só se o RoMa não puder correr); híbrido só se for escolhido.\nCorrespondências RoMa: correspondências densas trianguladas. Exige as licenças do RoMa v2 e do DINOv3 e o ponto de controlo.\nProfundidade MoGe: pontos dos mapas de profundidade monoculares do MoGe ajustados ao modelo esparso. Não exige o ponto de controlo do RoMa nem as suas licenças; os mapas em falta são criados pelo spirula geometry.\nHíbrido: primeiro as correspondências, a profundidade preenche o que falham. Exige o ponto de controlo do RoMa."),
    IT("Da dove vengono i nuovi punti.\nAutomatico: corrispondenze RoMa (le mappe di profondità solo se RoMa non può girare); ibrido solo se scelto.\nCorrispondenze RoMa: corrispondenze dense triangolate. Richiede le licenze di RoMa v2 e DINOv3 e il checkpoint.\nProfondità MoGe: punti dalle mappe di profondità monoculari MoGe adattate al modello sparso. Non richiede né il checkpoint RoMa né le sue licenze; le mappe mancanti le crea spirula geometry.\nIbrido: prima le corrispondenze, la profondità riempie ciò che mancano. Richiede il checkpoint RoMa."),
    NL("Waar de nieuwe punten vandaan komen.\nAutomatisch: RoMa-overeenkomsten (dieptekaarten alleen als RoMa niet kan draaien); hybride alleen als je het kiest.\nRoMa-overeenkomsten: getrianguleerde dichte overeenkomsten. Vereist de licenties van RoMa v2 en DINOv3 en het checkpoint.\nMoGe-diepte: punten uit monoculaire MoGe-dieptekaarten die op het ijle model zijn afgestemd. Vereist noch het RoMa-checkpoint noch de licenties ervan; ontbrekende kaarten maakt spirula geometry.\nHybride: eerst overeenkomsten, de diepte vult aan wat ze missen. Vereist het RoMa-checkpoint."),
    RU("Откуда берутся новые точки.\nАвто: соответствия RoMa (карты глубины — только если RoMa не может работать); гибрид — только по выбору.\nСоответствия RoMa: триангулированные плотные соответствия. Нужны лицензии RoMa v2 и DINOv3 и чекпойнт.\nГлубина MoGe: точки из монокулярных карт глубины MoGe, подогнанных к разреженной модели. Не нужны ни чекпойнт RoMa, ни его лицензии; недостающие карты строит spirula geometry.\nГибрид: сначала соответствия, глубина дополняет пропущенное. Нужен чекпойнт RoMa."),
    TR("Yeni noktaların kaynağı.\nOtomatik: RoMa eşleşmeleri (derinlik haritaları yalnızca RoMa çalışamıyorsa); karma yalnızca seçilirse.\nRoMa eşleşmeleri: üçgenlenmiş yoğun eşleşmeler. RoMa v2 ve DINOv3 lisansları ile kontrol noktası gerekir.\nMoGe derinliği: seyrek modele uydurulmuş MoGe tek gözlü derinlik haritalarından nokta üretir. RoMa kontrol noktası da lisansları da gerekmez; eksik haritaları spirula geometry üretir.\nKarma: önce eşleşmeler, kaçırdıklarını derinlik doldurur. RoMa kontrol noktası gerekir."));

SS_MSG(source_auto_roma_maps,
    EN("Auto will use RoMa matches here. This dataset has depth maps; choose Hybrid to use them."),
    JA("ここでは自動は RoMa のマッチになります。このデータセットには深度マップがあり、使うにはハイブリッドを選びます。"),
    ZH_HANS("此处自动将使用 RoMa 匹配。该数据集有深度图，选择混合即可使用。"),
    ZH_HANT("此處自動將使用 RoMa 匹配。該資料集有深度圖，選擇混合即可使用。"),
    KO("여기서는 자동이 RoMa 매칭을 사용합니다. 이 데이터셋에는 깊이 맵이 있으며, 쓰려면 하이브리드를 고르세요."),
    DE("Automatisch wählt hier RoMa-Zuordnungen. Dieser Datensatz hat Tiefenkarten; wähle Hybrid, um sie zu nutzen."),
    FR("Auto choisira les appariements RoMa ici. Ce jeu de données a des cartes de profondeur ; choisissez Hybride pour les utiliser."),
    ES("Automático usará correspondencias RoMa aquí. Este conjunto tiene mapas de profundidad; elige Híbrido para usarlos."),
    PT("Automático usará correspondências RoMa aqui. Este conjunto tem mapas de profundidade; escolha Híbrido para os usar."),
    IT("Automatico userà le corrispondenze RoMa qui. Questo dataset ha mappe di profondità; scegli Ibrido per usarle."),
    NL("Automatisch kiest hier RoMa-overeenkomsten. Deze dataset heeft dieptekaarten; kies Hybride om ze te gebruiken."),
    RU("Авто здесь выберет соответствия RoMa. В этом наборе есть карты глубины; чтобы их использовать, выберите Гибрид."),
    TR("Otomatik burada RoMa eşleşmelerini seçer. Bu veri kümesinde derinlik haritaları var; kullanmak için Karma'yı seçin."));

SS_MSG(source_auto_roma,
    EN("Auto will use RoMa matches here: this dataset has no depth maps."),
    JA("ここでは自動は RoMa のマッチになります: このデータセットには深度マップがありません。"),
    ZH_HANS("此处自动将使用 RoMa 匹配：该数据集没有深度图。"),
    ZH_HANT("此處自動將使用 RoMa 匹配：該資料集沒有深度圖。"),
    KO("여기서는 자동이 RoMa 매칭을 사용합니다: 이 데이터셋에 깊이 맵이 없습니다."),
    DE("Automatisch wählt hier RoMa-Zuordnungen: Dieser Datensatz hat keine Tiefenkarten."),
    FR("Auto choisira les appariements RoMa ici : ce jeu de données n'a pas de cartes de profondeur."),
    ES("Automático usará correspondencias RoMa aquí: este conjunto no tiene mapas de profundidad."),
    PT("Automático usará correspondências RoMa aqui: este conjunto não tem mapas de profundidade."),
    IT("Automatico userà le corrispondenze RoMa qui: questo dataset non ha mappe di profondità."),
    NL("Automatisch kiest hier RoMa-overeenkomsten: deze dataset heeft geen dieptekaarten."),
    RU("Авто здесь выберет соответствия RoMa: в этом наборе нет карт глубины."),
    TR("Otomatik burada RoMa eşleşmelerini seçer: bu veri kümesinde derinlik haritası yok."));

SS_MSG(source_moge_needs_none,
    EN("MoGe depth needs no RoMa checkpoint or licence."),
    JA("MoGe の深度には RoMa のチェックポイントもライセンスも不要です。"),
    ZH_HANS("MoGe 深度不需要 RoMa 检查点或许可。"),
    ZH_HANT("MoGe 深度不需要 RoMa 檢查點或授權。"),
    KO("MoGe 깊이에는 RoMa 체크포인트도 라이선스도 필요 없습니다."),
    DE("MoGe-Tiefe braucht weder RoMa-Checkpoint noch Lizenz."),
    FR("La profondeur MoGe ne demande ni point de contrôle ni licence RoMa."),
    ES("La profundidad MoGe no requiere punto de control ni licencia de RoMa."),
    PT("A profundidade MoGe não exige ponto de controlo nem licença do RoMa."),
    IT("La profondità MoGe non richiede né checkpoint né licenza RoMa."),
    NL("MoGe-diepte vereist geen RoMa-checkpoint of -licentie."),
    RU("Глубине MoGe не нужны ни чекпойнт RoMa, ни его лицензия."),
    TR("MoGe derinliği için RoMa kontrol noktası ya da lisansı gerekmez."));

SS_MSG(ckpt_ready,
    EN("RoMa v2 checkpoint ready"),
    JA("RoMa v2 のチェックポイントは準備できています"),
    ZH_HANS("RoMa v2 检查点已就绪"),
    ZH_HANT("RoMa v2 檢查點已就緒"),
    KO("RoMa v2 체크포인트 준비됨"),
    DE("RoMa-v2-Checkpoint bereit"),
    FR("Point de contrôle RoMa v2 prêt"),
    ES("Punto de control de RoMa v2 listo"),
    PT("Ponto de controlo do RoMa v2 pronto"),
    IT("Checkpoint di RoMa v2 pronto"),
    NL("RoMa v2-checkpoint gereed"),
    RU("Чекпойнт RoMa v2 готов"),
    TR("RoMa v2 kontrol noktası hazır"));

SS_MSG(ckpt_get,
    EN("Get the RoMa v2 checkpoint"),
    JA("RoMa v2 のチェックポイントを取得"),
    ZH_HANS("获取 RoMa v2 检查点"),
    ZH_HANT("取得 RoMa v2 檢查點"),
    KO("RoMa v2 체크포인트 받기"),
    DE("RoMa-v2-Checkpoint holen"),
    FR("Obtenir le point de contrôle RoMa v2"),
    ES("Obtener el punto de control de RoMa v2"),
    PT("Obter o ponto de controlo do RoMa v2"),
    IT("Scarica il checkpoint di RoMa v2"),
    NL("RoMa v2-checkpoint ophalen"),
    RU("Скачать чекпойнт RoMa v2"),
    TR("RoMa v2 kontrol noktasını al"));

SS_MSG(ckpt_first,
    EN("Get the RoMa v2 checkpoint first; the button is in the dense points options."),
    JA("先に RoMa v2 のチェックポイントを取得してください。ボタンは密な点のオプションにあります。"),
    ZH_HANS("请先获取 RoMa v2 检查点；按钮在稠密点选项中。"),
    ZH_HANT("請先取得 RoMa v2 檢查點；按鈕在稠密點選項中。"),
    KO("먼저 RoMa v2 체크포인트를 받으세요. 버튼은 조밀한 점 옵션에 있습니다."),
    DE("Holen Sie zuerst den RoMa-v2-Checkpoint; die Schaltfläche steht in den Optionen für dichte Punkte."),
    FR("Obtenez d'abord le point de contrôle RoMa v2 ; le bouton est dans les options des points denses."),
    ES("Obtén primero el punto de control de RoMa v2; el botón está en las opciones de puntos densos."),
    PT("Obtenha primeiro o ponto de controlo do RoMa v2; o botão está nas opções de pontos densos."),
    IT("Scarica prima il checkpoint di RoMa v2; il pulsante è nelle opzioni dei punti densi."),
    NL("Haal eerst het RoMa v2-checkpoint op; de knop staat bij de opties voor dichte punten."),
    RU("Сначала скачайте чекпойнт RoMa v2; кнопка в настройках плотных точек."),
    TR("Önce RoMa v2 kontrol noktasını alın; düğme yoğun nokta seçeneklerinde."));

SS_MSG(licence_declined,
    EN("A licence the dense points step needs was not accepted, so nothing was downloaded and no dense "
       "points were made."),
    JA("密な点に必要なライセンスに同意しなかったため、何もダウンロードせず、密な点も作成しませんでした。"),
    ZH_HANS("未接受稠密点所需的许可协议，因此没有下载任何内容，也没有生成稠密点。"),
    ZH_HANT("未接受稠密點所需的授權條款，因此沒有下載任何內容，也沒有產生稠密點。"),
    KO("조밀한 점에 필요한 라이선스에 동의하지 않아 아무것도 내려받지 않았고 조밀한 점도 만들지 않았습니다."),
    DE("Eine für dichte Punkte nötige Lizenz wurde nicht akzeptiert; es wurde nichts heruntergeladen und es "
       "entstanden keine dichten Punkte."),
    FR("Une licence nécessaire aux points denses n'a pas été acceptée : rien n'a été téléchargé et aucun "
       "point dense n'a été créé."),
    ES("No se aceptó una licencia que necesitan los puntos densos, así que no se descargó nada ni se "
       "crearon puntos densos."),
    PT("Não foi aceite uma licença de que os pontos densos precisam, por isso nada foi descarregado e não "
       "foram criados pontos densos."),
    IT("Una licenza necessaria ai punti densi non è stata accettata: non è stato scaricato nulla e non sono "
       "stati creati punti densi."),
    NL("Een voor dichte punten benodigde licentie is niet geaccepteerd, dus er is niets gedownload en er zijn "
       "geen dichte punten gemaakt."),
    RU("Лицензия, нужная для плотных точек, не принята, поэтому ничего не скачано и плотные точки не "
       "созданы."),
    TR("Yoğun noktalar için gereken bir lisans kabul edilmedi; bu yüzden hiçbir şey indirilmedi ve yoğun "
       "nokta oluşturulmadı."));

SS_MSG(advanced,
    EN("Dense points: overrides"),
    JA("密な点: 上書き設定"),
    ZH_HANS("稠密点：覆盖设置"),
    ZH_HANT("稠密點：覆寫設定"),
    KO("조밀한 점: 직접 지정"),
    DE("Dichte Punkte: Überschreibungen"),
    FR("Points denses : réglages manuels"),
    ES("Puntos densos: ajustes manuales"),
    PT("Pontos densos: definições manuais"),
    IT("Punti densi: impostazioni manuali"),
    NL("Dichte punten: overschrijvingen"),
    RU("Плотные точки: ручные настройки"),
    TR("Yoğun noktalar: elle ayarlar"));

SS_MSG(refs,
    EN("Reference views (0 = auto)"),
    JA("基準ビュー（0 = 自動）"),
    ZH_HANS("参考视图（0 = 自动）"),
    ZH_HANT("參考視圖（0 = 自動）"),
    KO("기준 뷰 (0 = 자동)"),
    DE("Referenzansichten (0 = automatisch)"),
    FR("Vues de référence (0 = auto)"),
    ES("Vistas de referencia (0 = auto)"),
    PT("Vistas de referência (0 = auto)"),
    IT("Viste di riferimento (0 = auto)"),
    NL("Referentiebeelden (0 = auto)"),
    RU("Опорные виды (0 = авто)"),
    TR("Başvuru görünümleri (0 = otomatik)"));

SS_MSG(neighbours,
    EN("Neighbours per view (0 = auto)"),
    JA("ビューごとの隣接数（0 = 自動）"),
    ZH_HANS("每个视图的邻居数（0 = 自动）"),
    ZH_HANT("每個視圖的鄰居數（0 = 自動）"),
    KO("뷰당 이웃 수 (0 = 자동)"),
    DE("Nachbarn pro Ansicht (0 = automatisch)"),
    FR("Voisins par vue (0 = auto)"),
    ES("Vecinas por vista (0 = auto)"),
    PT("Vizinhas por vista (0 = auto)"),
    IT("Vicine per vista (0 = auto)"),
    NL("Buren per beeld (0 = auto)"),
    RU("Соседей на вид (0 = авто)"),
    TR("Görünüm başına komşu (0 = otomatik)"));

SS_MSG(rule,
    EN("Neighbour rule"),
    JA("隣接の選び方"),
    ZH_HANS("邻居选择规则"),
    ZH_HANT("鄰居選擇規則"),
    KO("이웃 선택 방식"),
    DE("Nachbarregel"),
    FR("Règle de voisinage"),
    ES("Regla de vecindad"),
    PT("Regra de vizinhança"),
    IT("Regola dei vicini"),
    NL("Burenregel"),
    RU("Правило выбора соседей"),
    TR("Komşu kuralı"));

SS_MSG(matches,
    EN("Matches per view (0 = auto)"),
    JA("ビューごとのマッチ数（0 = 自動）"),
    ZH_HANS("每个视图的匹配数（0 = 自动）"),
    ZH_HANT("每個視圖的匹配數（0 = 自動）"),
    KO("뷰당 매칭 수 (0 = 자동)"),
    DE("Treffer pro Ansicht (0 = automatisch)"),
    FR("Appariements par vue (0 = auto)"),
    ES("Correspondencias por vista (0 = auto)"),
    PT("Correspondências por vista (0 = auto)"),
    IT("Corrispondenze per vista (0 = auto)"),
    NL("Koppelingen per beeld (0 = auto)"),
    RU("Соответствий на вид (0 = авто)"),
    TR("Görünüm başına eşleşme (0 = otomatik)"));

SS_MSG(max_points,
    EN("Max points (0 = auto)"),
    JA("最大点数（0 = 自動）"),
    ZH_HANS("最大点数（0 = 自动）"),
    ZH_HANT("最大點數（0 = 自動）"),
    KO("최대 점 수 (0 = 자동)"),
    DE("Maximale Punktzahl (0 = automatisch)"),
    FR("Nombre max. de points (0 = auto)"),
    ES("Máximo de puntos (0 = auto)"),
    PT("Máximo de pontos (0 = auto)"),
    IT("Massimo di punti (0 = auto)"),
    NL("Max. punten (0 = auto)"),
    RU("Макс. число точек (0 = авто)"),
    TR("En çok nokta (0 = otomatik)"));

SS_MSG(min_track,
    EN("Min images per point (0 = auto)"),
    JA("1 点あたりの最小画像数（0 = 自動）"),
    ZH_HANS("每点最少图像数（0 = 自动）"),
    ZH_HANT("每點最少影像數（0 = 自動）"),
    KO("점당 최소 이미지 수 (0 = 자동)"),
    DE("Mindestbilder pro Punkt (0 = automatisch)"),
    FR("Images min. par point (0 = auto)"),
    ES("Mín. de imágenes por punto (0 = auto)"),
    PT("Mín. de imagens por ponto (0 = auto)"),
    IT("Immagini minime per punto (0 = auto)"),
    NL("Min. beelden per punt (0 = auto)"),
    RU("Мин. снимков на точку (0 = авто)"),
    TR("Nokta başına en az görüntü (0 = otomatik)"));

SS_MSG(use_masks,
    EN("Keep masked pixels out"),
    JA("マスクした画素を除く"),
    ZH_HANS("排除被遮罩的像素"),
    ZH_HANT("排除被遮罩的像素"),
    KO("마스크된 픽셀 제외"),
    DE("Maskierte Pixel ausschließen"),
    FR("Exclure les pixels masqués"),
    ES("Excluir los píxeles enmascarados"),
    PT("Excluir os píxeis mascarados"),
    IT("Escludi i pixel mascherati"),
    NL("Gemaskeerde pixels uitsluiten"),
    RU("Исключить замаскированные пиксели"),
    TR("Maskelenen pikselleri dışarıda tut"));

SS_MSG(step_name,
    EN("Dense points"),
    JA("密な点"),
    ZH_HANS("稠密点"),
    ZH_HANT("稠密點"),
    KO("조밀한 점"),
    DE("Dichte Punkte"),
    FR("Points denses"),
    ES("Puntos densos"),
    PT("Pontos densos"),
    IT("Punti densi"),
    NL("Dichte punten"),
    RU("Плотные точки"),
    TR("Yoğun noktalar"));

SS_MSG(stage_dense,
    EN("Adding dense points"),
    JA("密な点を加えています"),
    ZH_HANS("正在添加稠密点"),
    ZH_HANT("正在加入稠密點"),
    KO("조밀한 점을 추가하는 중"),
    DE("Dichte Punkte werden hinzugefügt"),
    FR("Ajout des points denses"),
    ES("Añadiendo puntos densos"),
    PT("A adicionar pontos densos"),
    IT("Aggiunta dei punti densi"),
    NL("Dichte punten toevoegen"),
    RU("Добавление плотных точек"),
    TR("Yoğun noktalar ekleniyor"));

SS_MSG(rerun,
    EN("Dense points again"),
    JA("密な点をやり直す"),
    ZH_HANS("重算稠密点"),
    ZH_HANT("重算稠密點"),
    KO("조밀한 점 다시 만들기"),
    DE("Dichte Punkte neu"),
    FR("Refaire les points denses"),
    ES("Rehacer los puntos densos"),
    PT("Refazer os pontos densos"),
    IT("Rifai i punti densi"),
    NL("Dichte punten opnieuw"),
    RU("Заново плотные точки"),
    TR("Yoğun noktaları yeniden yap"));

SS_MSG(rerun_help,
    EN("Match and triangulate again and replace the dense model on disk. The sparse model is not touched."),
    JA("マッチングと三角測量をやり直し、ディスク上の密なモデルを置き換えます。スパースなモデルには触れません。"),
    ZH_HANS("重新匹配并三角化，替换磁盘上的稠密模型。稀疏模型不受影响。"),
    ZH_HANT("重新匹配並三角化，取代磁碟上的稠密模型。稀疏模型不受影響。"),
    KO("매칭과 삼각측량을 다시 하고 디스크의 조밀한 모델을 바꿉니다. 희소 모델은 건드리지 않습니다."),
    DE("Gleicht und trianguliert erneut und ersetzt das dichte Modell auf der Platte. Das dünne Modell bleibt "
       "unberührt."),
    FR("Refait l'appariement et la triangulation et remplace le modèle dense sur le disque. Le modèle épars "
       "n'est pas touché."),
    ES("Vuelve a emparejar y triangular y reemplaza el modelo denso del disco. El modelo disperso no se toca."),
    PT("Volta a emparelhar e triangular e substitui o modelo denso no disco. O modelo esparso não é tocado."),
    IT("Abbina e triangola di nuovo e sostituisce il modello denso su disco. Il modello sparso non viene "
       "toccato."),
    NL("Koppelt en trianguleert opnieuw en vervangt het dichte model op schijf. Het ijle model blijft "
       "onaangeroerd."),
    RU("Заново сопоставляет и триангулирует и заменяет плотную модель на диске. Разреженная модель не "
       "затрагивается."),
    TR("Yeniden eşleştirir ve üçgenler, diskteki yoğun modeli değiştirir. Seyrek modele dokunulmaz."));

SS_MSG(plan_current,
    EN("the dense model is up to date; nothing to match"),
    JA("密なモデルは最新です。マッチングするものはありません"),
    ZH_HANS("稠密模型是最新的；无需匹配"),
    ZH_HANT("稠密模型是最新的；無需匹配"),
    KO("조밀한 모델이 최신입니다. 매칭할 것이 없습니다"),
    DE("das dichte Modell ist aktuell; nichts abzugleichen"),
    FR("le modèle dense est à jour ; rien à apparier"),
    ES("el modelo denso está al día; nada que emparejar"),
    PT("o modelo denso está atualizado; nada a emparelhar"),
    IT("il modello denso è aggiornato; niente da abbinare"),
    NL("het dichte model is actueel; niets te koppelen"),
    RU("плотная модель актуальна; сопоставлять нечего"),
    TR("yoğun model güncel; eşleştirilecek bir şey yok"));

SS_MSG(plan_changed,
    EN("the dense model was made with other settings ({0}); making it again"),
    JA("密なモデルは別の設定（{0}）で作られています。作り直します"),
    ZH_HANS("稠密模型使用了其他设置（{0}）；将重新生成"),
    ZH_HANT("稠密模型使用了其他設定（{0}）；將重新產生"),
    KO("조밀한 모델이 다른 설정({0})으로 만들어졌습니다. 다시 만듭니다"),
    DE("das dichte Modell entstand mit anderen Einstellungen ({0}); wird neu erzeugt"),
    FR("le modèle dense a été fait avec d'autres réglages ({0}) ; il est refait"),
    ES("el modelo denso se hizo con otros ajustes ({0}); se rehace"),
    PT("o modelo denso foi feito com outras definições ({0}); a refazer"),
    IT("il modello denso è stato fatto con altre impostazioni ({0}); lo rifaccio"),
    NL("het dichte model is met andere instellingen gemaakt ({0}); opnieuw maken"),
    RU("плотная модель создана с другими настройками ({0}); создаётся заново"),
    TR("yoğun model başka ayarlarla yapılmış ({0}); yeniden yapılıyor"));

SS_MSG(plan_stale,
    EN("the frames or the reconstruction under the dense model have changed; making it again"),
    JA("密なモデルの元になったフレームか再構成が変わりました。作り直します"),
    ZH_HANS("稠密模型所依据的帧或重建已改变；将重新生成"),
    ZH_HANT("稠密模型所依據的影格或重建已改變；將重新產生"),
    KO("조밀한 모델의 바탕인 프레임이나 재구성이 바뀌었습니다. 다시 만듭니다"),
    DE("die Frames oder die Rekonstruktion unter dem dichten Modell haben sich geändert; wird neu erzeugt"),
    FR("les images ou la reconstruction sous le modèle dense ont changé ; il est refait"),
    ES("los fotogramas o la reconstrucción bajo el modelo denso cambiaron; se rehace"),
    PT("os fotogramas ou a reconstrução sob o modelo denso mudaram; a refazer"),
    IT("i fotogrammi o la ricostruzione sotto il modello denso sono cambiati; lo rifaccio"),
    NL("de frames of de reconstructie onder het dichte model zijn gewijzigd; opnieuw maken"),
    RU("кадры или реконструкция, на которых построена плотная модель, изменились; создаётся заново"),
    TR("yoğun modelin dayandığı kareler veya yeniden yapılandırma değişti; yeniden yapılıyor"));

SS_MSG(err_failed,
    EN("adding dense points failed (see the log). The reconstruction itself is finished and can be "
       "trained on as it is."),
    JA("密な点の追加に失敗しました（ログを参照）。再構成そのものは完了しており、そのまま学習に使えます。"),
    ZH_HANS("添加稠密点失败（见日志）。重建本身已完成，可以直接用于训练。"),
    ZH_HANT("加入稠密點失敗（見記錄）。重建本身已完成，可以直接用於訓練。"),
    KO("조밀한 점 추가에 실패했습니다(로그 참조). 재구성 자체는 끝났으며 그대로 학습에 쓸 수 있습니다."),
    DE("Das Hinzufügen dichter Punkte ist fehlgeschlagen (siehe Protokoll). Die Rekonstruktion selbst ist "
       "fertig und kann unverändert trainiert werden."),
    FR("l'ajout des points denses a échoué (voir le journal). La reconstruction elle-même est terminée et "
       "peut servir à l'entraînement telle quelle."),
    ES("falló la adición de puntos densos (mira el registro). La reconstrucción en sí está terminada y se "
       "puede entrenar tal como está."),
    PT("falhou a adição de pontos densos (veja o registo). A reconstrução em si está concluída e pode ser "
       "treinada tal como está."),
    IT("l'aggiunta dei punti densi è fallita (vedi il registro). La ricostruzione in sé è finita e si può "
       "addestrare così com'è."),
    NL("het toevoegen van dichte punten is mislukt (zie het logboek). De reconstructie zelf is klaar en kan "
       "zo getraind worden."),
    RU("добавить плотные точки не удалось (см. журнал). Сама реконструкция завершена, и на ней можно "
       "обучать как есть."),
    TR("yoğun nokta ekleme başarısız oldu (günlüğe bakın). Yeniden yapılandırmanın kendisi bitti ve olduğu "
       "gibi eğitilebilir."));

SS_MSG(err_spawn,
    EN("could not start the dense points step ({0})"),
    JA("密な点のステップを起動できませんでした（{0}）"),
    ZH_HANS("无法启动稠密点步骤（{0}）"),
    ZH_HANT("無法啟動稠密點步驟（{0}）"),
    KO("조밀한 점 단계를 시작하지 못했습니다({0})"),
    DE("der Schritt für dichte Punkte ließ sich nicht starten ({0})"),
    FR("impossible de lancer l'étape des points denses ({0})"),
    ES("no se pudo iniciar el paso de puntos densos ({0})"),
    PT("não foi possível iniciar o passo de pontos densos ({0})"),
    IT("impossibile avviare il passo dei punti densi ({0})"),
    NL("de stap voor dichte punten kon niet starten ({0})"),
    RU("не удалось запустить шаг плотных точек ({0})"),
    TR("yoğun nokta adımı başlatılamadı ({0})"));

SS_MSG(err_no_model,
    EN("there is no COLMAP model to add dense points to"),
    JA("密な点を加える COLMAP モデルがありません"),
    ZH_HANS("没有可添加稠密点的 COLMAP 模型"),
    ZH_HANT("沒有可加入稠密點的 COLMAP 模型"),
    KO("조밀한 점을 추가할 COLMAP 모델이 없습니다"),
    DE("es gibt kein COLMAP-Modell, dem dichte Punkte hinzugefügt werden könnten"),
    FR("il n'y a aucun modèle COLMAP auquel ajouter des points denses"),
    ES("no hay ningún modelo COLMAP al que añadir puntos densos"),
    PT("não há nenhum modelo COLMAP ao qual adicionar pontos densos"),
    IT("non c'è alcun modello COLMAP a cui aggiungere punti densi"),
    NL("er is geen COLMAP-model om dichte punten aan toe te voegen"),
    RU("нет модели COLMAP, к которой можно добавить плотные точки"),
    TR("yoğun nokta eklenecek bir COLMAP modeli yok"));

SS_MSG(err_license,
    EN("the RoMa v2 and DINOv3 licences have not been accepted"),
    JA("RoMa v2 と DINOv3 のライセンスに同意していません"),
    ZH_HANS("尚未接受 RoMa v2 和 DINOv3 的许可协议"),
    ZH_HANT("尚未接受 RoMa v2 和 DINOv3 的授權條款"),
    KO("RoMa v2와 DINOv3 라이선스에 동의하지 않았습니다"),
    DE("die Lizenzen von RoMa v2 und DINOv3 wurden nicht akzeptiert"),
    FR("les licences de RoMa v2 et de DINOv3 n'ont pas été acceptées"),
    ES("no se han aceptado las licencias de RoMa v2 y DINOv3"),
    PT("as licenças do RoMa v2 e do DINOv3 não foram aceites"),
    IT("le licenze di RoMa v2 e DINOv3 non sono state accettate"),
    NL("de licenties van RoMa v2 en DINOv3 zijn niet geaccepteerd"),
    RU("лицензии RoMa v2 и DINOv3 не приняты"),
    TR("RoMa v2 ve DINOv3 lisansları kabul edilmedi"));

SS_MSG(chk_unavailable,
    EN("This build cannot add dense points."),
    JA("このビルドは密な点を加えられません。"),
    ZH_HANS("此版本无法添加稠密点。"),
    ZH_HANT("此版本無法加入稠密點。"),
    KO("이 빌드는 조밀한 점을 추가할 수 없습니다."),
    DE("Dieser Build kann keine dichten Punkte hinzufügen."),
    FR("Cette version ne peut pas ajouter de points denses."),
    ES("Esta compilación no puede añadir puntos densos."),
    PT("Esta compilação não consegue adicionar pontos densos."),
    IT("Questa build non può aggiungere punti densi."),
    NL("Deze build kan geen dichte punten toevoegen."),
    RU("Эта сборка не умеет добавлять плотные точки."),
    TR("Bu derleme yoğun nokta ekleyemez."));

SS_MSG(chk_not_ready,
    EN("The RoMa v2 checkpoint is still missing, or its licences are not accepted: the download failed or "
       "was declined. Fetch it on the New Dataset screen, then start the batch again."),
    JA("RoMa v2 のチェックポイントがまだ無いか、ライセンスに同意していません。ダウンロードが"
       "失敗したか、拒否されました。「新規データセット」画面で取得してから、バッチをもう一度開始して"
       "ください。"),
    ZH_HANS("RoMa v2 检查点仍然缺失，或尚未接受其许可协议：下载失败或被拒绝。请先在“新建数据集”"
            "界面获取，然后重新开始批处理。"),
    ZH_HANT("RoMa v2 檢查點仍然缺失，或尚未接受其授權條款：下載失敗或被拒絕。請先在「新增資料集」"
            "畫面取得，然後重新開始批次。"),
    KO("RoMa v2 체크포인트가 아직 없거나 라이선스에 동의하지 않았습니다. 내려받기가 "
       "실패했거나 거부되었습니다. '새 데이터셋' 화면에서 받은 다음 배치를 다시 시작하세요."),
    DE("Der RoMa-v2-Checkpoint fehlt noch, oder seine Lizenzen wurden nicht akzeptiert: Der Download ist "
       "fehlgeschlagen oder wurde abgelehnt. Im Bildschirm „Neuer Datensatz“ holen und den Stapel dann "
       "erneut starten."),
    FR("Le point de contrôle RoMa v2 manque encore, ou ses licences n'ont pas été acceptées : le "
       "téléchargement a échoué ou a été refusé. Récupérez-le dans l'écran Nouveau jeu de données, puis "
       "relancez le lot."),
    ES("El punto de control de RoMa v2 sigue sin estar, o sus licencias no se han aceptado: la descarga "
       "falló o se rechazó. Descárgalo en la pantalla Nuevo conjunto y vuelve a iniciar el lote."),
    PT("O ponto de controlo do RoMa v2 continua em falta, ou as suas licenças não foram aceites: a "
       "transferência falhou ou foi recusada. Obtenha-o no ecrã Novo conjunto e volte a iniciar o lote."),
    IT("Il checkpoint di RoMa v2 manca ancora, oppure le sue licenze non sono state accettate: il download è "
       "fallito o è stato rifiutato. Scaricalo nella schermata Nuovo dataset e riavvia il batch."),
    NL("Het RoMa v2-checkpoint ontbreekt nog, of de licenties zijn niet geaccepteerd: de download is mislukt "
       "of geweigerd. Haal het op in het scherm Nieuwe dataset en start de batch opnieuw."),
    RU("Контрольная точка RoMa v2 по-прежнему отсутствует, или её лицензии не приняты: загрузка не удалась "
       "или была отклонена. Загрузите её на экране «Новый набор данных» и запустите пакет снова."),
    TR("RoMa v2 kontrol noktası hâlâ yok ya da lisansları kabul edilmedi: indirme başarısız oldu veya "
       "reddedildi. Onu Yeni Veri Kümesi ekranından alın, sonra toplu işi yeniden başlatın."));

SS_MSG(panel_button,
    EN("Add Dense Points"),
    JA("密な点を加える"),
    ZH_HANS("添加稠密点"),
    ZH_HANT("加入稠密點"),
    KO("조밀한 점 추가"),
    DE("Dichte Punkte hinzufügen"),
    FR("Ajouter des points denses"),
    ES("Añadir puntos densos"),
    PT("Adicionar pontos densos"),
    IT("Aggiungi punti densi"),
    NL("Dichte punten toevoegen"),
    RU("Добавить плотные точки"),
    TR("Yoğun nokta ekle"));

SS_MSG(panel_button_help,
    EN("A dense, coloured point cloud for the cameras this dataset already has, from RoMa v2 matches. "
       "It is written as a new model beside the sparse one; the cameras and poses stay exactly as they are."),
    JA("このデータセットにすでにあるカメラに対して、RoMa v2 のマッチから色付きの密な点群を作ります。"
       "スパースなモデルの隣に新しいモデルとして書き出し、カメラと姿勢はそのまま変わりません。"),
    ZH_HANS("为本数据集已有的相机，根据 RoMa v2 匹配生成带颜色的稠密点云。结果写成稀疏模型旁的新模型；"
            "相机和位姿完全保持不变。"),
    ZH_HANT("為本資料集已有的相機，根據 RoMa v2 匹配產生帶顏色的稠密點雲。結果寫成稀疏模型旁的新模型；"
            "相機和位姿完全保持不變。"),
    KO("이 데이터셋에 이미 있는 카메라에 대해 RoMa v2 매칭으로 색이 있는 조밀한 점군을 만듭니다. 희소 모델 옆의 "
       "새 모델로 쓰며, 카메라와 포즈는 그대로 유지됩니다."),
    DE("Eine dichte, farbige Punktwolke für die Kameras, die dieser Datensatz schon hat, aus RoMa-v2-Treffern. "
       "Sie wird als neues Modell neben dem dünnen geschrieben; Kameras und Posen bleiben genau, wie sie sind."),
    FR("Un nuage de points dense et coloré pour les caméras que ce jeu de données a déjà, à partir "
       "d'appariements RoMa v2. Il est écrit comme un nouveau modèle à côté du modèle épars ; les caméras et "
       "les poses restent exactement telles quelles."),
    ES("Una nube de puntos densa y con color para las cámaras que este conjunto ya tiene, a partir de "
       "correspondencias de RoMa v2. Se escribe como un modelo nuevo junto al disperso; las cámaras y las poses "
       "quedan exactamente como están."),
    PT("Uma nuvem de pontos densa e colorida para as câmaras que este conjunto já tem, a partir de "
       "correspondências do RoMa v2. É escrita como um modelo novo ao lado do esparso; as câmaras e as poses "
       "ficam exatamente como estão."),
    IT("Una nuvola di punti densa e colorata per le camere che questo insieme ha già, da corrispondenze RoMa v2. "
       "Viene scritta come nuovo modello accanto a quello sparso; camere e pose restano esattamente come sono."),
    NL("Een dichte, gekleurde puntenwolk voor de camera's die deze dataset al heeft, uit RoMa v2-koppelingen. "
       "Ze wordt als nieuw model naast het ijle geschreven; camera's en poses blijven precies zoals ze zijn."),
    RU("Плотное цветное облако точек для камер, которые уже есть в этом наборе, по соответствиям RoMa v2. "
       "Записывается как новая модель рядом с разреженной; камеры и позы остаются как есть."),
    TR("Bu veri kümesinin zaten sahip olduğu kameralar için RoMa v2 eşleşmelerinden yoğun, renkli bir nokta "
       "bulutu. Seyrek modelin yanına yeni bir model olarak yazılır; kameralar ve pozlar olduğu gibi kalır."));

SS_MSG(panel_explain,
    EN("Keeps every camera in {0} exactly as it is and writes the dense points to {1}. {0} is never "
       "written. Train on it by picking {1} in the Model list."),
    JA("{0} のカメラはすべてそのままにして、密な点を {1} に書き出します。{0} には書き込みません。"
       "学習するには「モデル」の一覧で {1} を選びます。"),
    ZH_HANS("保持 {0} 中所有相机不变，并把稠密点写入 {1}。{0} 从不写入。要用它训练，请在“模型”列表中选择 {1}。"),
    ZH_HANT("保持 {0} 中所有相機不變，並把稠密點寫入 {1}。{0} 從不寫入。要用它訓練，請在「模型」清單中選擇 {1}。"),
    KO("{0}의 모든 카메라를 그대로 두고 조밀한 점을 {1}에 씁니다. {0}에는 쓰지 않습니다. 학습하려면 "
       "'모델' 목록에서 {1}을 고르세요."),
    DE("Lässt jede Kamera in {0} genau, wie sie ist, und schreibt die dichten Punkte nach {1}. {0} wird nie "
       "geschrieben. Zum Trainieren {1} in der Modellliste wählen."),
    FR("Garde chaque caméra de {0} exactement telle quelle et écrit les points denses dans {1}. {0} n'est "
       "jamais écrit. Pour entraîner dessus, choisissez {1} dans la liste Modèle."),
    ES("Deja cada cámara de {0} exactamente como está y escribe los puntos densos en {1}. {0} nunca se "
       "escribe. Para entrenar con él, elige {1} en la lista Modelo."),
    PT("Mantém cada câmara de {0} exatamente como está e escreve os pontos densos em {1}. {0} nunca é "
       "escrito. Para treinar com ele, escolha {1} na lista Modelo."),
    IT("Lascia ogni camera di {0} esattamente com'è e scrive i punti densi in {1}. {0} non viene mai scritto. "
       "Per addestrare su di esso, scegli {1} nell'elenco Modello."),
    NL("Laat elke camera in {0} precies zoals ze is en schrijft de dichte punten naar {1}. {0} wordt nooit "
       "geschreven. Train erop door {1} te kiezen in de lijst Model."),
    RU("Оставляет каждую камеру в {0} как есть и записывает плотные точки в {1}. {0} не записывается. Чтобы "
       "обучать на ней, выберите {1} в списке «Модель»."),
    TR("{0} içindeki her kamerayı olduğu gibi bırakır ve yoğun noktaları {1} içine yazar. {0} hiç yazılmaz. "
       "Üzerinde eğitmek için Model listesinden {1} seçin."));

SS_MSG(panel_run,
    EN("Add dense points"),
    JA("密な点を加える"),
    ZH_HANS("添加稠密点"),
    ZH_HANT("加入稠密點"),
    KO("조밀한 점 추가"),
    DE("Dichte Punkte hinzufügen"),
    FR("Ajouter des points denses"),
    ES("Añadir puntos densos"),
    PT("Adicionar pontos densos"),
    IT("Aggiungi punti densi"),
    NL("Dichte punten toevoegen"),
    RU("Добавить плотные точки"),
    TR("Yoğun nokta ekle"));

SS_MSG(panel_replace,
    EN("Replace the dense model that is already there"),
    JA("すでにある密なモデルを置き換える"),
    ZH_HANS("替换已有的稠密模型"),
    ZH_HANT("取代已有的稠密模型"),
    KO("이미 있는 조밀한 모델을 바꾸기"),
    DE("Das schon vorhandene dichte Modell ersetzen"),
    FR("Remplacer le modèle dense déjà présent"),
    ES("Reemplazar el modelo denso que ya existe"),
    PT("Substituir o modelo denso que já existe"),
    IT("Sostituisci il modello denso già presente"),
    NL("Het al aanwezige dichte model vervangen"),
    RU("Заменить уже существующую плотную модель"),
    TR("Zaten var olan yoğun modeli değiştir"));

SS_MSG(panel_done,
    EN("Dense points: wrote {0} points to {1}"),
    JA("密な点: {0} 点を {1} に書き出しました"),
    ZH_HANS("稠密点：已将 {0} 个点写入 {1}"),
    ZH_HANT("稠密點：已將 {0} 個點寫入 {1}"),
    KO("조밀한 점: {0}개 점을 {1}에 썼습니다"),
    DE("Dichte Punkte: {0} Punkte nach {1} geschrieben"),
    FR("Points denses : {0} points écrits dans {1}"),
    ES("Puntos densos: {0} puntos escritos en {1}"),
    PT("Pontos densos: {0} pontos escritos em {1}"),
    IT("Punti densi: scritti {0} punti in {1}"),
    NL("Dichte punten: {0} punten naar {1} geschreven"),
    RU("Плотные точки: {0} точек записано в {1}"),
    TR("Yoğun noktalar: {0} nokta {1} içine yazıldı"));

SS_MSG(panel_failed,
    EN("Dense points failed (exit code {0}); see the lines above. {1} was not changed."),
    JA("密な点の追加に失敗しました（終了コード {0}）。上の行を参照してください。{1} は変更されていません。"),
    ZH_HANS("稠密点失败（退出码 {0}）；见上方各行。{1} 未被改动。"),
    ZH_HANT("稠密點失敗（結束碼 {0}）；見上方各行。{1} 未被更動。"),
    KO("조밀한 점 추가 실패(종료 코드 {0}). 위의 줄을 참조하세요. {1}은 바뀌지 않았습니다."),
    DE("Dichte Punkte fehlgeschlagen (Exit-Code {0}); siehe die Zeilen darüber. {1} wurde nicht verändert."),
    FR("Échec des points denses (code de sortie {0}) ; voir les lignes ci-dessus. {1} n'a pas été modifié."),
    ES("Fallaron los puntos densos (código de salida {0}); mira las líneas anteriores. {1} no se modificó."),
    PT("Falharam os pontos densos (código de saída {0}); veja as linhas acima. {1} não foi alterado."),
    IT("Punti densi falliti (codice di uscita {0}); vedi le righe sopra. {1} non è stato modificato."),
    NL("Dichte punten mislukt (afsluitcode {0}); zie de regels hierboven. {1} is niet gewijzigd."),
    RU("Плотные точки не получились (код выхода {0}); см. строки выше. {1} не изменён."),
    TR("Yoğun noktalar başarısız (çıkış kodu {0}); yukarıdaki satırlara bakın. {1} değişmedi."));

SS_MSG(panel_cancelled,
    EN("Dense points cancelled; nothing was written."),
    JA("密な点の追加を中止しました。何も書き込んでいません。"),
    ZH_HANS("已取消稠密点；未写入任何内容。"),
    ZH_HANT("已取消稠密點；未寫入任何內容。"),
    KO("조밀한 점 추가를 취소했습니다. 아무것도 쓰지 않았습니다."),
    DE("Dichte Punkte abgebrochen; es wurde nichts geschrieben."),
    FR("Points denses annulés ; rien n'a été écrit."),
    ES("Puntos densos cancelados; no se escribió nada."),
    PT("Pontos densos cancelados; nada foi escrito."),
    IT("Punti densi annullati; non è stato scritto nulla."),
    NL("Dichte punten geannuleerd; er is niets geschreven."),
    RU("Добавление плотных точек отменено; ничего не записано."),
    TR("Yoğun noktalar iptal edildi; hiçbir şey yazılmadı."));

SS_MSG(panel_spawn_failed,
    EN("Could not start {0}."),
    JA("{0} を起動できませんでした。"),
    ZH_HANS("无法启动 {0}。"),
    ZH_HANT("無法啟動 {0}。"),
    KO("{0}을(를) 시작하지 못했습니다."),
    DE("{0} ließ sich nicht starten."),
    FR("Impossible de lancer {0}."),
    ES("No se pudo iniciar {0}."),
    PT("Não foi possível iniciar {0}."),
    IT("Impossibile avviare {0}."),
    NL("{0} kon niet starten."),
    RU("Не удалось запустить {0}."),
    TR("{0} başlatılamadı."));

// ---- the training screen's model chooser ----

SS_MSG(model_combo,
    EN("Model"),
    JA("モデル"),
    ZH_HANS("模型"),
    ZH_HANT("模型"),
    KO("모델"),
    DE("Modell"),
    FR("Modèle"),
    ES("Modelo"),
    PT("Modelo"),
    IT("Modello"),
    NL("Model"),
    RU("Модель"),
    TR("Model"));

SS_MSG(model_combo_help,
    EN("Which COLMAP model of this dataset to train on: its cameras, and its points as the seed. "
       "sparse/0-roma is the dense one Add Dense Points made. Choosing one reloads the dataset."),
    JA("このデータセットのどの COLMAP モデルで学習するか。そのカメラと、シードとなる点を使います。"
       "sparse/0-roma は「密な点を加える」で作った密なモデルです。選ぶとデータセットを読み込み直します。"),
    ZH_HANS("用本数据集的哪个 COLMAP 模型训练：使用其相机，并以其点作为种子。sparse/0-roma 是“添加稠密点”生成的"
            "稠密模型。选择后会重新加载数据集。"),
    ZH_HANT("用本資料集的哪個 COLMAP 模型訓練：使用其相機，並以其點作為種子。sparse/0-roma 是「加入稠密點」產生的"
            "稠密模型。選擇後會重新載入資料集。"),
    KO("이 데이터셋의 어느 COLMAP 모델로 학습할지 고릅니다. 그 카메라와 시드가 되는 점을 씁니다. "
       "sparse/0-roma는 '조밀한 점 추가'로 만든 조밀한 모델입니다. 고르면 데이터셋을 다시 불러옵니다."),
    DE("Mit welchem COLMAP-Modell dieses Datensatzes trainiert wird: seine Kameras und seine Punkte als Saat. "
       "sparse/0-roma ist das dichte Modell aus „Dichte Punkte hinzufügen“. Die Wahl lädt den Datensatz neu."),
    FR("Avec quel modèle COLMAP de ce jeu de données entraîner : ses caméras et ses points comme graine. "
       "sparse/0-roma est le modèle dense créé par Ajouter des points denses. Le choix recharge le jeu de "
       "données."),
    ES("Con qué modelo COLMAP de este conjunto entrenar: sus cámaras y sus puntos como semilla. sparse/0-roma "
       "es el denso creado por Añadir puntos densos. Al elegir se recarga el conjunto."),
    PT("Com que modelo COLMAP deste conjunto treinar: as suas câmaras e os seus pontos como semente. "
       "sparse/0-roma é o denso criado por Adicionar pontos densos. Ao escolher, o conjunto é recarregado."),
    IT("Con quale modello COLMAP di questo insieme addestrare: le sue camere e i suoi punti come seme. "
       "sparse/0-roma è quello denso creato da Aggiungi punti densi. La scelta ricarica l'insieme."),
    NL("Met welk COLMAP-model van deze dataset wordt getraind: zijn camera's en zijn punten als zaad. "
       "sparse/0-roma is het dichte model uit Dichte punten toevoegen. Een keuze laadt de dataset opnieuw."),
    RU("На какой модели COLMAP этого набора обучать: её камеры и её точки как затравка. sparse/0-roma — плотная "
       "модель, созданная командой «Добавить плотные точки». Выбор перезагружает набор."),
    TR("Bu veri kümesinin hangi COLMAP modeliyle eğitileceği: kameraları ve tohum olarak noktaları. "
       "sparse/0-roma, Yoğun nokta ekle ile yapılan yoğun modeldir. Seçim veri kümesini yeniden yükler."));

SS_MSG(model_entry,
    EN("{0}  ({1} points, {2} images)"),
    JA("{0}  （{1} 点、画像 {2} 枚）"),
    ZH_HANS("{0}  （{1} 个点，{2} 张图像）"),
    ZH_HANT("{0}  （{1} 個點，{2} 張影像）"),
    KO("{0}  (점 {1}개, 이미지 {2}장)"),
    DE("{0}  ({1} Punkte, {2} Bilder)"),
    FR("{0}  ({1} points, {2} images)"),
    ES("{0}  ({1} puntos, {2} imágenes)"),
    PT("{0}  ({1} pontos, {2} imagens)"),
    IT("{0}  ({1} punti, {2} immagini)"),
    NL("{0}  ({1} punten, {2} beelden)"),
    RU("{0}  ({1} точек, {2} снимков)"),
    TR("{0}  ({1} nokta, {2} görüntü)"));

SS_MSG(err_cloud_mismatch,
    EN("the dense cloud written does not match the checksums densify.json recorded for it, so it was not "
       "used (see the log)."),
    JA("書き込まれた密な点群が densify.json に記録されたチェックサムと一致しないため、使われませんでした（ログを参照）。"),
    ZH_HANS("写出的稠密点云与 densify.json 中记录的校验和不一致，因此未被使用（见日志）。"),
    ZH_HANT("寫出的稠密點雲與 densify.json 中記錄的檢查碼不一致，因此未被使用（見記錄）。"),
    KO("기록된 조밀한 점군이 densify.json에 적힌 체크섬과 맞지 않아 사용하지 않았습니다(로그 참조)."),
    DE("Die geschriebene dichte Wolke stimmt nicht mit den in densify.json vermerkten Prüfsummen überein und "
       "wurde nicht verwendet (siehe Protokoll)."),
    FR("le nuage dense écrit ne correspond pas aux sommes de contrôle notées dans densify.json ; il n'a pas "
       "été utilisé (voir le journal)."),
    ES("la nube densa escrita no coincide con las sumas de comprobación de densify.json, así que no se usó "
       "(mira el registro)."),
    PT("a nuvem densa escrita não coincide com as somas de verificação de densify.json, por isso não foi "
       "usada (veja o registo)."),
    IT("la nuvola densa scritta non corrisponde ai checksum registrati in densify.json, quindi non è stata "
       "usata (vedi il registro)."),
    NL("de geschreven dichte wolk komt niet overeen met de controlesommen in densify.json en is niet "
       "gebruikt (zie het logboek)."),
    RU("записанное плотное облако не совпадает с контрольными суммами из densify.json, поэтому оно не "
       "использовано (см. журнал)."),
    TR("yazılan yoğun bulut densify.json'daki sağlama toplamlarıyla uyuşmuyor, bu yüzden kullanılmadı "
       "(günlüğe bakın)."));

SS_MSG(model_entry_bad,
    EN("{0}  ({1} points, {2} images)  [checksum mismatch]"),
    JA("{0}  （{1} 点、画像 {2} 枚）  [チェックサム不一致]"),
    ZH_HANS("{0}  （{1} 个点，{2} 张图像）  [校验和不符]"),
    ZH_HANT("{0}  （{1} 個點，{2} 張影像）  [檢查碼不符]"),
    KO("{0}  (점 {1}개, 이미지 {2}장)  [체크섬 불일치]"),
    DE("{0}  ({1} Punkte, {2} Bilder)  [Prüfsumme stimmt nicht]"),
    FR("{0}  ({1} points, {2} images)  [somme de contrôle incorrecte]"),
    ES("{0}  ({1} puntos, {2} imágenes)  [suma de comprobación distinta]"),
    PT("{0}  ({1} pontos, {2} imagens)  [soma de verificação diferente]"),
    IT("{0}  ({1} punti, {2} immagini)  [checksum non corrispondente]"),
    NL("{0}  ({1} punten, {2} beelden)  [controlesom komt niet overeen]"),
    RU("{0}  ({1} точек, {2} снимков)  [контрольная сумма не совпадает]"),
    TR("{0}  ({1} nokta, {2} görüntü)  [sağlama toplamı uyuşmuyor]"));

SS_MSG(model_entry_ok,
    EN("{0}  ({1} points, {2} images)  [checksum ok]"),
    JA("{0}  （{1} 点、画像 {2} 枚）  [チェックサム一致]"),
    ZH_HANS("{0}  （{1} 个点，{2} 张图像）  [校验和相符]"),
    ZH_HANT("{0}  （{1} 個點，{2} 張影像）  [檢查碼相符]"),
    KO("{0}  (점 {1}개, 이미지 {2}장)  [체크섬 일치]"),
    DE("{0}  ({1} Punkte, {2} Bilder)  [Prüfsumme stimmt]"),
    FR("{0}  ({1} points, {2} images)  [somme de contrôle correcte]"),
    ES("{0}  ({1} puntos, {2} imágenes)  [suma de comprobación correcta]"),
    PT("{0}  ({1} pontos, {2} imagens)  [soma de verificação correta]"),
    IT("{0}  ({1} punti, {2} immagini)  [checksum corretto]"),
    NL("{0}  ({1} punten, {2} beelden)  [controlesom klopt]"),
    RU("{0}  ({1} точек, {2} снимков)  [контрольная сумма совпала]"),
    TR("{0}  ({1} nokta, {2} görüntü)  [sağlama toplamı doğru]"));

SS_MSG(model_auto_entry,
    EN("Auto (most images)"),
    JA("自動（画像が最も多いもの）"),
    ZH_HANS("自动（图像最多的）"),
    ZH_HANT("自動（影像最多的）"),
    KO("자동 (이미지가 가장 많은 것)"),
    DE("Automatisch (meiste Bilder)"),
    FR("Auto (le plus d'images)"),
    ES("Automático (más imágenes)"),
    PT("Automático (mais imagens)"),
    IT("Automatico (più immagini)"),
    NL("Automatisch (meeste beelden)"),
    RU("Авто (больше всего снимков)"),
    TR("Otomatik (en çok görüntü)"));

// LEGAL -- human review in every language. The DINOv3 summary states only what
// the Agreement says; it adds no term and drops none.
SS_MSG(license_dinov3_title,
    EN("DINOv3 License Agreement (Meta)"),
    JA("DINOv3 ライセンス契約（Meta）"),
    ZH_HANS("DINOv3 许可协议（Meta）"),
    ZH_HANT("DINOv3 授權協議（Meta）"),
    KO("DINOv3 라이선스 계약(Meta)"),
    DE("DINOv3-Lizenzvereinbarung (Meta)"),
    FR("Contrat de licence DINOv3 (Meta)"),
    ES("Acuerdo de licencia de DINOv3 (Meta)"),
    PT("Contrato de licença do DINOv3 (Meta)"),
    IT("Accordo di licenza DINOv3 (Meta)"),
    NL("DINOv3-licentieovereenkomst (Meta)"),
    RU("Лицензионное соглашение DINOv3 (Meta)"),
    TR("DINOv3 Lisans Sözleşmesi (Meta)"));

SS_MSG(license_dinov3_summary,
    EN("The RoMa v2 file contains Meta's DINOv3 model weights. They are Meta's, "
       "not part of Spirula Studio, and come with Meta's DINOv3 License "
       "Agreement, shown in full below. Please read it: accepting it here means "
       "you agree to be bound by it. The file is downloaded from the RoMa v2 "
       "authors' release; it is never bundled with the app."),
    JA("RoMa v2 のファイルには、Meta の DINOv3 モデルの重みが含まれています。"
       "これは Meta のものであり Spirula Studio の一部ではなく、Meta の DINOv3 "
       "ライセンス契約が適用されます。契約の全文を下に表示しますので、お読みください。"
       "ここで同意すると、この契約に拘束されることに同意したことになります。"
       "ファイルは RoMa v2 の作者のリリースからダウンロードされ、アプリに同梱される"
       "ことはありません。"),
    ZH_HANS("RoMa v2 的文件包含 Meta 的 DINOv3 模型权重。它们属于 Meta，不属于 "
            "Spirula Studio，并适用 Meta 的 DINOv3 许可协议，协议全文显示在下方。"
            "请阅读：在此接受即表示你同意受其约束。该文件从 RoMa v2 作者的发布页下载，"
            "不会随应用一起打包。"),
    ZH_HANT("RoMa v2 的檔案包含 Meta 的 DINOv3 模型權重。它們屬於 Meta，不屬於 "
            "Spirula Studio，並適用 Meta 的 DINOv3 授權協議，協議全文顯示在下方。"
            "請閱讀：在此接受即表示你同意受其約束。該檔案從 RoMa v2 作者的發布頁下載，"
            "不會隨應用程式一起打包。"),
    KO("RoMa v2 파일에는 Meta의 DINOv3 모델 가중치가 들어 있습니다. 이는 Meta의 "
       "것이며 Spirula Studio의 일부가 아니고, 아래에 전문을 표시한 Meta의 DINOv3 "
       "라이선스 계약이 적용됩니다. 읽어 보세요. 여기서 동의하면 이 계약에 구속되는 "
       "데 동의하는 것입니다. 파일은 RoMa v2 저자의 릴리스에서 내려받으며 앱에 "
       "포함되지 않습니다."),
    DE("Die RoMa-v2-Datei enthält die DINOv3-Modellgewichte von Meta. Sie gehören "
       "Meta, sind nicht Teil von Spirula Studio und unterliegen Metas DINOv3 "
       "License Agreement, das unten vollständig angezeigt wird. Bitte lesen Sie "
       "es: Mit der Annahme hier erklären Sie sich daran gebunden. Die Datei wird "
       "aus dem Release der RoMa-v2-Autoren heruntergeladen und nie mit der "
       "Anwendung mitgeliefert."),
    FR("Le fichier RoMa v2 contient les poids du modèle DINOv3 de Meta. Ils "
       "appartiennent à Meta, ne font pas partie de Spirula Studio et sont soumis "
       "au DINOv3 License Agreement de Meta, affiché en entier ci-dessous. Lisez-le "
       ": l'accepter ici signifie que vous acceptez d'être lié par ses termes. Le "
       "fichier est téléchargé depuis la publication des auteurs de RoMa v2 ; il "
       "n'est jamais fourni avec l'application."),
    ES("El archivo de RoMa v2 contiene los pesos del modelo DINOv3 de Meta. Son de "
       "Meta, no forman parte de Spirula Studio y se rigen por el DINOv3 License "
       "Agreement de Meta, que se muestra completo abajo. Léelo: al aceptarlo aquí "
       "aceptas quedar obligado por él. El archivo se descarga de la publicación de "
       "los autores de RoMa v2; nunca se incluye con la aplicación."),
    PT("O arquivo do RoMa v2 contém os pesos do modelo DINOv3 da Meta. Eles "
       "pertencem à Meta, não fazem parte do Spirula Studio e estão sujeitos ao "
       "DINOv3 License Agreement da Meta, exibido por inteiro abaixo. Leia-o: ao "
       "aceitá-lo aqui, você concorda em ficar vinculado a ele. O arquivo é baixado "
       "da release dos autores do RoMa v2 e nunca é incluído no aplicativo."),
    IT("Il file di RoMa v2 contiene i pesi del modello DINOv3 di Meta. Sono di "
       "Meta, non fanno parte di Spirula Studio e sono soggetti al DINOv3 License "
       "Agreement di Meta, mostrato per intero qui sotto. Leggilo: accettandolo qui "
       "accetti di esserne vincolato. Il file viene scaricato dalla release degli "
       "autori di RoMa v2 e non viene mai incluso nell'app."),
    NL("Het RoMa v2-bestand bevat de DINOv3-modelgewichten van Meta. Die zijn van "
       "Meta, maken geen deel uit van Spirula Studio en vallen onder Meta's DINOv3 "
       "License Agreement, hieronder volledig weergegeven. Lees het: door het hier "
       "te aanvaarden stem je ermee in eraan gebonden te zijn. Het bestand wordt "
       "gedownload van de release van de auteurs van RoMa v2 en wordt nooit met de "
       "app meegeleverd."),
    RU("Файл RoMa v2 содержит веса модели DINOv3 от Meta. Они принадлежат Meta, не "
       "входят в Spirula Studio и распространяются на условиях Meta DINOv3 License "
       "Agreement, полный текст которого показан ниже. Прочитайте его: приняв его "
       "здесь, вы соглашаетесь быть связанным его условиями. Файл загружается из "
       "релиза авторов RoMa v2 и никогда не поставляется вместе с приложением."),
    TR("RoMa v2 dosyası, Meta'nın DINOv3 model ağırlıklarını içerir. Bunlar Meta'ya "
       "aittir, Spirula Studio'nun parçası değildir ve tam metni aşağıda gösterilen "
       "Meta'nın DINOv3 License Agreement'ına tabidir. Lütfen okuyun: burada kabul "
       "etmeniz, onunla bağlı olmayı kabul ettiğiniz anlamına gelir. Dosya, RoMa v2 "
       "yazarlarının sürümünden indirilir; uygulamayla birlikte asla verilmez."));

SS_MSG(license_romav2_title,
    EN("RoMa v2 licence (MIT)"),
    JA("RoMa v2 のライセンス（MIT）"),
    ZH_HANS("RoMa v2 许可协议（MIT）"),
    ZH_HANT("RoMa v2 授權條款（MIT）"),
    KO("RoMa v2 라이선스(MIT)"),
    DE("Lizenz von RoMa v2 (MIT)"),
    FR("Licence de RoMa v2 (MIT)"),
    ES("Licencia de RoMa v2 (MIT)"),
    PT("Licença do RoMa v2 (MIT)"),
    IT("Licenza di RoMa v2 (MIT)"),
    NL("Licentie van RoMa v2 (MIT)"),
    RU("Лицензия RoMa v2 (MIT)"),
    TR("RoMa v2 lisansı (MIT)"));

SS_MSG(license_romav2_summary,
    EN("RoMa v2 (Johan Edstedt) is released under the MIT licence, shown in full "
       "below. The model file is downloaded from its authors' release rather than "
       "bundled with the app."),
    JA("RoMa v2（Johan Edstedt）は MIT ライセンスで公開されています。全文を下に"
       "表示します。モデルのファイルは、アプリに同梱せず、作者のリリースから"
       "ダウンロードします。"),
    ZH_HANS("RoMa v2（Johan Edstedt）以 MIT 许可协议发布，全文显示在下方。"
            "模型文件从作者的发布页下载，而不是随应用打包。"),
    ZH_HANT("RoMa v2（Johan Edstedt）以 MIT 授權條款發布，全文顯示在下方。"
            "模型檔案從作者的發布頁下載，而不是隨應用程式打包。"),
    KO("RoMa v2(Johan Edstedt)는 MIT 라이선스로 공개되어 있으며 전문은 아래에 "
       "표시됩니다. 모델 파일은 앱에 포함하지 않고 저자의 릴리스에서 내려받습니다."),
    DE("RoMa v2 (Johan Edstedt) steht unter der MIT-Lizenz, unten vollständig "
       "angezeigt. Die Modelldatei wird aus dem Release der Autoren heruntergeladen "
       "und nicht mit der Anwendung mitgeliefert."),
    FR("RoMa v2 (Johan Edstedt) est publié sous licence MIT, affichée en entier "
       "ci-dessous. Le fichier du modèle est téléchargé depuis la publication de ses "
       "auteurs et n'est pas fourni avec l'application."),
    ES("RoMa v2 (Johan Edstedt) se publica bajo la licencia MIT, que se muestra "
       "completa abajo. El archivo del modelo se descarga de la publicación de sus "
       "autores y no se incluye con la aplicación."),
    PT("O RoMa v2 (Johan Edstedt) é publicado sob a licença MIT, exibida por inteiro "
       "abaixo. O arquivo do modelo é baixado da release dos autores e não é "
       "incluído no aplicativo."),
    IT("RoMa v2 (Johan Edstedt) è pubblicato con licenza MIT, mostrata per intero "
       "qui sotto. Il file del modello viene scaricato dalla release dei suoi autori "
       "e non è incluso nell'app."),
    NL("RoMa v2 (Johan Edstedt) is uitgebracht onder de MIT-licentie, hieronder "
       "volledig weergegeven. Het modelbestand wordt gedownload van de release van "
       "de auteurs en niet met de app meegeleverd."),
    RU("RoMa v2 (Johan Edstedt) выпущен под лицензией MIT, полный текст которой "
       "показан ниже. Файл модели загружается из релиза его авторов и не "
       "поставляется вместе с приложением."),
    TR("RoMa v2 (Johan Edstedt) MIT lisansıyla yayımlanmıştır; tam metni aşağıda "
       "gösterilir. Model dosyası, uygulamayla birlikte verilmek yerine yazarlarının "
       "sürümünden indirilir."));


}}}}
#include "i18n/EndCatalog.h"
