#pragma once

// What `spirula densify` says: its --help and every line a run prints. Flag
// names and the words they take (`auto`, `covis`, `off`) are identifiers and
// stay as they are; `--check` is a deep diagnostic and prints English.

#include "i18n/BeginCatalog.h"

namespace spirula {
namespace i18n {
namespace msg {
namespace densify {

SS_MSG(tagline,
    EN("Dense points for a solved model, triangulated from dense matches between its images"),
    JA("解かれたモデルに、画像間の密なマッチから三角測量した密な点を加えます"),
    ZH_HANS("为已求解的模型生成稠密点：由图像间的稠密匹配三角化得到"),
    ZH_HANT("為已求解的模型產生稠密點：由影像間的稠密匹配三角化得到"),
    KO("해결된 모델에 이미지 사이의 조밀한 매칭으로 삼각측량한 조밀한 점을 만듭니다"),
    DE("Dichte Punkte für ein gelöstes Modell, trianguliert aus dichten Zuordnungen seiner Bilder"),
    FR("Des points denses pour un modèle résolu, triangulés à partir d'appariements denses entre ses images"),
    ES("Puntos densos para un modelo resuelto, triangulados a partir de correspondencias densas entre sus imágenes"),
    PT("Pontos densos para um modelo resolvido, triangulados a partir de correspondências densas entre as suas imagens"),
    IT("Punti densi per un modello risolto, triangolati da corrispondenze dense tra le sue immagini"),
    NL("Dichte punten voor een opgelost model, getrianguleerd uit dichte overeenkomsten tussen de beelden"),
    RU("Плотные точки для решённой модели, триангулированные по плотным соответствиям между её снимками"),
    TR("Çözülmüş bir model için, görüntüleri arasındaki yoğun eşleşmelerden üçgenlenen yoğun noktalar"));

SS_MSG(usage_target,
    EN("The dataset folder holding a COLMAP sparse/ model. The dense cloud goes into a new "
       "model beside it, sparse/<model>-roma, whose cameras and poses are copied byte for "
       "byte; the source model is never written. Train on it with --colmap-recon-dir."),
    JA("COLMAP の sparse/ モデルを含むデータセットのフォルダです。密な点群はその隣の新しいモデル "
       "sparse/<model>-roma に書き出し、カメラと姿勢はバイト単位でそのまま写します。元のモデルには"
       "書き込みません。学習には --colmap-recon-dir で指定します。"),
    ZH_HANS("包含 COLMAP sparse/ 模型的数据集文件夹。稠密点云写入旁边的新模型 sparse/<model>-roma，"
            "相机与位姿逐字节复制，源模型从不写入。训练时用 --colmap-recon-dir 指定它。"),
    ZH_HANT("包含 COLMAP sparse/ 模型的資料集資料夾。稠密點雲寫入旁邊的新模型 sparse/<model>-roma，"
            "相機與位姿逐位元組複製，來源模型從不寫入。訓練時用 --colmap-recon-dir 指定它。"),
    KO("COLMAP sparse/ 모델이 있는 데이터셋 폴더입니다. 조밀한 점은 그 옆의 새 모델 "
       "sparse/<model>-roma 에 쓰고, 카메라와 자세는 바이트 그대로 복사합니다. 원본 모델에는 "
       "쓰지 않습니다. 학습할 때는 --colmap-recon-dir 로 지정합니다."),
    DE("Der Datensatzordner mit einem COLMAP-sparse/-Modell. Die dichte Wolke kommt in ein neues "
       "Modell daneben, sparse/<model>-roma, dessen Kameras und Posen Byte für Byte kopiert "
       "werden; das Quellmodell wird nie geschrieben. Trainieren mit --colmap-recon-dir."),
    FR("Le dossier du jeu de données contenant un modèle COLMAP sparse/. Le nuage dense va dans "
       "un nouveau modèle à côté, sparse/<model>-roma, dont les caméras et les poses sont copiées "
       "octet pour octet ; le modèle source n'est jamais écrit. Entraînez dessus avec "
       "--colmap-recon-dir."),
    ES("La carpeta del conjunto con un modelo COLMAP sparse/. La nube densa va a un modelo nuevo "
       "a su lado, sparse/<model>-roma, cuyas cámaras y poses se copian byte a byte; el modelo "
       "de origen nunca se escribe. Entrena con él usando --colmap-recon-dir."),
    PT("A pasta do conjunto com um modelo COLMAP sparse/. A nuvem densa vai para um modelo novo "
       "ao lado, sparse/<model>-roma, cujas câmaras e poses são copiadas byte a byte; o modelo de "
       "origem nunca é escrito. Treine com ele usando --colmap-recon-dir."),
    IT("La cartella dell'insieme con un modello COLMAP sparse/. La nuvola densa va in un nuovo "
       "modello accanto, sparse/<model>-roma, le cui camere e pose sono copiate byte per byte; il "
       "modello di origine non viene mai scritto. Addestra su di esso con --colmap-recon-dir."),
    NL("De datasetmap met een COLMAP-sparse/-model. De dichte wolk komt in een nieuw model "
       "ernaast, sparse/<model>-roma, waarvan camera's en poses byte voor byte worden gekopieerd; "
       "het bronmodel wordt nooit geschreven. Train erop met --colmap-recon-dir."),
    RU("Папка набора с моделью COLMAP sparse/. Плотное облако записывается в новую модель рядом, "
       "sparse/<model>-roma, камеры и позы которой копируются байт в байт; исходная модель не "
       "изменяется. Обучение на ней: --colmap-recon-dir."),
    TR("COLMAP sparse/ modeli içeren veri kümesi klasörü. Yoğun bulut yanına yeni bir modele, "
       "sparse/<model>-roma, yazılır; kameraları ve pozları bayt bayt kopyalanır, kaynak model "
       "hiç yazılmaz. Onunla --colmap-recon-dir ile eğitin."));

SS_MSG(head_options,
    EN("Options"), JA("オプション"), ZH_HANS("选项"), ZH_HANT("選項"),
    KO("옵션"), DE("Optionen"), FR("Options"), ES("Opciones"), PT("Opções"),
    IT("Opzioni"), NL("Opties"), RU("Параметры"), TR("Seçenekler"));

SS_MSG(head_auto,
    EN("Every setting is chosen from the input unless given; the run prints what it chose."),
    JA("指定しない設定はすべて入力から選び、選んだ値を表示します。"),
    ZH_HANS("未指定的设置均根据输入自动选择，运行时会打印所选的值。"),
    ZH_HANT("未指定的設定均根據輸入自動選擇，執行時會列印所選的值。"),
    KO("지정하지 않은 설정은 모두 입력에서 고르고, 고른 값을 출력합니다."),
    DE("Jede nicht angegebene Einstellung wird aus der Eingabe gewählt; der Lauf zeigt die Wahl an."),
    FR("Tout réglage non donné est choisi d'après l'entrée ; l'exécution affiche ce qu'elle a choisi."),
    ES("Cada ajuste no indicado se elige a partir de la entrada; la ejecución muestra lo elegido."),
    PT("Cada definição não indicada é escolhida a partir da entrada; a execução mostra o que escolheu."),
    IT("Ogni impostazione non indicata è scelta dall'ingresso; l'esecuzione stampa la scelta."),
    NL("Elke niet opgegeven instelling wordt uit de invoer gekozen; de run toont de keuze."),
    RU("Всё, что не задано, выбирается по входным данным; запуск печатает сделанный выбор."),
    TR("Verilmeyen her ayar girdiden seçilir; çalışma neyi seçtiğini yazdırır."));

#define SS_DENSIFY_OPT(name, en, ja, zhs, zht, ko, de, fr, es, pt, it, nl, ru, tr)            \
    SS_MSG(name, EN(en), JA(ja), ZH_HANS(zhs), ZH_HANT(zht), KO(ko), DE(de), FR(fr), ES(es), \
           PT(pt), IT(it), NL(nl), RU(ru), TR(tr));

SS_DENSIFY_OPT(opt_model,
    "The source model, relative to the dataset. Default: the one the trainer picks",
    "元のモデル（データセットからの相対パス）。既定: 学習が選ぶもの",
    "源模型，相对于数据集。默认：训练器选择的那个",
    "來源模型，相對於資料集。預設：訓練器選擇的那個",
    "원본 모델, 데이터셋 기준 상대 경로. 기본값: 학습기가 고르는 모델",
    "Das Quellmodell, relativ zum Datensatz. Standard: das, das der Trainer wählt",
    "Le modèle source, relatif au jeu de données. Défaut : celui que l'entraîneur choisit",
    "El modelo de origen, relativo al conjunto. Por defecto: el que elige el entrenador",
    "O modelo de origem, relativo ao conjunto. Por omissão: o que o treinador escolhe",
    "Il modello di origine, relativo all'insieme. Predefinito: quello scelto dall'addestramento",
    "Het bronmodel, relatief aan de dataset. Standaard: het model dat de trainer kiest",
    "Исходная модель относительно набора. По умолчанию: та, что выбирает обучение",
    "Kaynak model, veri kümesine göre. Varsayılan: eğiticinin seçtiği")
SS_DENSIFY_OPT(opt_out,
    "Where the dense model goes. Default: <model>-roma beside the source",
    "密なモデルの書き出し先。既定: 元のモデルの隣の <model>-roma",
    "稠密模型的输出位置。默认：源模型旁边的 <model>-roma",
    "稠密模型的輸出位置。預設：來源模型旁邊的 <model>-roma",
    "조밀한 모델을 쓸 곳. 기본값: 원본 옆의 <model>-roma",
    "Wohin das dichte Modell kommt. Standard: <model>-roma neben der Quelle",
    "Où va le modèle dense. Défaut : <model>-roma à côté de la source",
    "Dónde va el modelo denso. Por defecto: <model>-roma junto al origen",
    "Para onde vai o modelo denso. Por omissão: <model>-roma ao lado da origem",
    "Dove va il modello denso. Predefinito: <model>-roma accanto all'origine",
    "Waar het dichte model komt. Standaard: <model>-roma naast de bron",
    "Куда записать плотную модель. По умолчанию: <model>-roma рядом с исходной",
    "Yoğun modelin yazılacağı yer. Varsayılan: kaynağın yanında <model>-roma")
SS_DENSIFY_OPT(opt_image_dir,
    "The images, relative to the dataset or absolute. Default: images",
    "画像のフォルダ（相対または絶対パス）。既定: images",
    "图像文件夹，相对或绝对路径。默认：images",
    "影像資料夾，相對或絕對路徑。預設：images",
    "이미지 폴더, 상대 또는 절대 경로. 기본값: images",
    "Die Bilder, relativ zum Datensatz oder absolut. Standard: images",
    "Les images, relatives au jeu de données ou absolues. Défaut : images",
    "Las imágenes, relativas al conjunto o absolutas. Por defecto: images",
    "As imagens, relativas ao conjunto ou absolutas. Por omissão: images",
    "Le immagini, relative all'insieme o assolute. Predefinito: images",
    "De beelden, relatief aan de dataset of absoluut. Standaard: images",
    "Снимки, относительно набора или абсолютный путь. По умолчанию: images",
    "Görüntüler, veri kümesine göre ya da mutlak. Varsayılan: images")
SS_DENSIFY_OPT(opt_mask_dir,
    "Masks, white where the image is kept, read as the trainer reads them. Default: masks",
    "マスク。画像を残す所が白で、学習と同じように読みます。既定: masks",
    "蒙版，白色为保留区域，读取方式与训练器相同。默认：masks",
    "遮罩，白色為保留區域，讀取方式與訓練器相同。預設：masks",
    "마스크, 이미지를 남기는 곳이 흰색이며 학습기와 같이 읽습니다. 기본값: masks",
    "Masken, weiß wo das Bild bleibt, gelesen wie vom Trainer. Standard: masks",
    "Masques, blancs là où l'image est gardée, lus comme par l'entraîneur. Défaut : masks",
    "Máscaras, blancas donde se conserva la imagen, leídas como el entrenador. Por defecto: masks",
    "Máscaras, brancas onde a imagem é mantida, lidas como o treinador. Por omissão: masks",
    "Maschere, bianche dove l'immagine resta, lette come dall'addestramento. Predefinito: masks",
    "Maskers, wit waar het beeld blijft, gelezen zoals de trainer ze leest. Standaard: masks",
    "Маски, белые там, где снимок сохраняется, читаются как при обучении. По умолчанию: masks",
    "Maskeler; görüntünün tutulduğu yer beyaz, eğitici gibi okunur. Varsayılan: masks")
SS_DENSIFY_OPT(opt_flip_mask,
    "Masks that paint the region to remove instead",
    "取り除く領域を塗ったマスクとして扱う",
    "蒙版改为标出要去除的区域",
    "遮罩改為標出要去除的區域",
    "제거할 영역을 칠한 마스크로 다룸",
    "Masken, die stattdessen den zu entfernenden Bereich malen",
    "Masques qui peignent plutôt la zone à retirer",
    "Máscaras que pintan en cambio la zona a quitar",
    "Máscaras que pintam antes a zona a remover",
    "Maschere che dipingono invece la zona da togliere",
    "Maskers die juist het te verwijderen gebied tekenen",
    "Маски, которые закрашивают удаляемую область",
    "Bunun yerine kaldırılacak bölgeyi boyayan maskeler")
SS_DENSIFY_OPT(opt_preset,
    "RoMa v2's match resolution: turbo 320, fast 512, base 640, high 640 then 960, precise 800 then 1280. Default: base",
    "RoMa v2 のマッチ解像度: turbo 320、fast 512、base 640、high 640 から 960、precise 800 から 1280。既定: base",
    "RoMa v2 的匹配分辨率：turbo 320，fast 512，base 640，high 先 640 后 960，precise 先 800 后 1280。默认：base",
    "RoMa v2 的匹配解析度：turbo 320，fast 512，base 640，high 先 640 後 960，precise 先 800 後 1280。預設：base",
    "RoMa v2 의 매칭 해상도: turbo 320, fast 512, base 640, high 640 다음 960, precise 800 다음 1280. 기본값: base",
    "Vergleichsauflösung von RoMa v2: turbo 320, fast 512, base 640, high 640 dann 960, precise 800 dann 1280. Standard: base",
    "Résolution d'appariement de RoMa v2 : turbo 320, fast 512, base 640, high 640 puis 960, precise 800 puis 1280. Défaut : base",
    "Resolución de emparejamiento de RoMa v2: turbo 320, fast 512, base 640, high 640 y luego 960, precise 800 y luego 1280. Por defecto: base",
    "Resolução de emparelhamento do RoMa v2: turbo 320, fast 512, base 640, high 640 e depois 960, precise 800 e depois 1280. Por omissão: base",
    "Risoluzione di accoppiamento di RoMa v2: turbo 320, fast 512, base 640, high 640 poi 960, precise 800 poi 1280. Predefinito: base",
    "Koppelresolutie van RoMa v2: turbo 320, fast 512, base 640, high 640 dan 960, precise 800 dan 1280. Standaard: base",
    "Разрешение сопоставления RoMa v2: turbo 320, fast 512, base 640, high 640 затем 960, precise 800 затем 1280. По умолчанию: base",
    "RoMa v2 eşleme çözünürlüğü: turbo 320, fast 512, base 640, high önce 640 sonra 960, precise önce 800 sonra 1280. Varsayılan: base")
SS_DENSIFY_OPT(opt_matches,
    "Read the matches from .rwm files in this folder instead of running RoMa v2",
    "RoMa v2 を実行せず、このフォルダの .rwm ファイルからマッチを読む",
    "不运行 RoMa v2，从此文件夹的 .rwm 文件读取匹配",
    "不執行 RoMa v2，從此資料夾的 .rwm 檔案讀取匹配",
    "RoMa v2 를 실행하지 않고 이 폴더의 .rwm 파일에서 매칭을 읽음",
    "Die Zuordnungen aus .rwm-Dateien in diesem Ordner lesen, statt RoMa v2 auszuführen",
    "Lire les appariements dans les fichiers .rwm de ce dossier au lieu de lancer RoMa v2",
    "Leer las correspondencias de los archivos .rwm de esta carpeta en lugar de ejecutar RoMa v2",
    "Ler as correspondências dos ficheiros .rwm desta pasta em vez de executar o RoMa v2",
    "Leggere le corrispondenze dai file .rwm di questa cartella invece di eseguire RoMa v2",
    "De overeenkomsten uit .rwm-bestanden in deze map lezen in plaats van RoMa v2 te draaien",
    "Читать соответствия из файлов .rwm в этой папке вместо запуска RoMa v2",
    "RoMa v2 çalıştırmak yerine eşleşmeleri bu klasördeki .rwm dosyalarından oku")
SS_DENSIFY_OPT(opt_export_pairs,
    "Write the views to match and the list of pairs to this folder, then stop",
    "マッチさせるビューとペアの一覧をこのフォルダに書き出して終了",
    "把要匹配的视图和图像对列表写入此文件夹，然后停止",
    "把要匹配的視圖和影像對列表寫入此資料夾，然後停止",
    "매칭할 뷰와 쌍 목록을 이 폴더에 쓰고 멈춤",
    "Die zu vergleichenden Ansichten und die Paarliste in diesen Ordner schreiben, dann aufhören",
    "Écrire les vues à apparier et la liste des paires dans ce dossier, puis s'arrêter",
    "Escribir las vistas a emparejar y la lista de pares en esta carpeta, y parar",
    "Escrever as vistas a emparelhar e a lista de pares nesta pasta, e parar",
    "Scrivere le viste da accoppiare e l'elenco delle coppie in questa cartella, poi fermarsi",
    "De te koppelen aanzichten en de lijst van paren naar deze map schrijven en stoppen",
    "Записать виды для сопоставления и список пар в эту папку и остановиться",
    "Eşlenecek görünümleri ve çift listesini bu klasöre yaz, sonra dur")
SS_DENSIFY_OPT(opt_plugin_exact,
    "Reproduce the Lichtfeld densification plugin's host stage, defects included (parity checks)",
    "Lichtfeld の高密度化プラグインのホスト処理を不具合ごと再現する（一致確認用）",
    "按原样重现 Lichtfeld 稠密化插件的主机阶段，包括其缺陷（用于一致性检查）",
    "按原樣重現 Lichtfeld 稠密化外掛的主機階段，包括其缺陷（用於一致性檢查）",
    "Lichtfeld 조밀화 플러그인의 호스트 단계를 결함까지 그대로 재현 (일치 확인용)",
    "Die Host-Stufe des Lichtfeld-Verdichtungs-Plugins samt Fehlern nachbilden (Paritätsprüfung)",
    "Reproduire l'étape hôte du greffon de densification Lichtfeld, défauts compris (contrôles de parité)",
    "Reproducir la etapa de host del complemento de densificación de Lichtfeld, defectos incluidos (paridad)",
    "Reproduzir a etapa de anfitrião do plugin de densificação Lichtfeld, defeitos incluídos (paridade)",
    "Riprodurre la fase host del plugin di densificazione Lichtfeld, difetti compresi (verifiche di parità)",
    "De hoststap van de Lichtfeld-verdichtingsplug-in nabootsen, fouten inbegrepen (pariteitscontrole)",
    "Воспроизвести хост-этап плагина уплотнения Lichtfeld вместе с его дефектами (проверка совпадения)",
    "Lichtfeld yoğunlaştırma eklentisinin ana makine aşamasını hatalarıyla birlikte yeniden üret (eşlik denetimi)")
SS_DENSIFY_OPT(opt_refs,
    "Reference images: a fraction of the images, or a count. Default: 0.8",
    "基準画像: 画像全体に対する割合、または枚数。既定: 0.8",
    "参考图像：占全部图像的比例或张数。默认：0.8",
    "參考影像：佔全部影像的比例或張數。預設：0.8",
    "기준 이미지: 전체에 대한 비율 또는 개수. 기본값: 0.8",
    "Referenzbilder: ein Anteil der Bilder oder eine Anzahl. Standard: 0.8",
    "Images de référence : une fraction des images, ou un nombre. Défaut : 0.8",
    "Imágenes de referencia: una fracción de las imágenes o una cantidad. Por defecto: 0.8",
    "Imagens de referência: uma fração das imagens ou uma quantidade. Por omissão: 0.8",
    "Immagini di riferimento: una frazione delle immagini o un numero. Predefinito: 0.8",
    "Referentiebeelden: een fractie van de beelden of een aantal. Standaard: 0.8",
    "Опорные снимки: доля всех снимков или число. По умолчанию: 0.8",
    "Referans görüntüler: görüntülerin bir oranı ya da sayısı. Varsayılan: 0.8")
SS_DENSIFY_OPT(opt_neighbours,
    "Images each reference is matched against. Default: 3",
    "各基準画像をマッチさせる画像の数。既定: 3",
    "每张参考图像与之匹配的图像数。默认：3",
    "每張參考影像與之匹配的影像數。預設：3",
    "기준 이미지마다 매칭할 이미지 수. 기본값: 3",
    "Bilder, mit denen jede Referenz verglichen wird. Standard: 3",
    "Images avec lesquelles chaque référence est appariée. Défaut : 3",
    "Imágenes con las que se empareja cada referencia. Por defecto: 3",
    "Imagens com que cada referência é emparelhada. Por omissão: 3",
    "Immagini con cui ogni riferimento è accoppiato. Predefinito: 3",
    "Beelden waarmee elke referentie wordt gekoppeld. Standaard: 3",
    "Снимки, с которыми сопоставляется каждый опорный. По умолчанию: 3",
    "Her referansın eşlendiği görüntüler. Varsayılan: 3")
SS_DENSIFY_OPT(opt_neighbour_rule,
    "covis: the most shared sparse points with enough parallax; pose: the plugin's nearest pose",
    "covis: 視差が十分で共有する疎な点が最も多いもの。pose: プラグインの最も近い姿勢",
    "covis：视差足够且共享稀疏点最多者；pose：插件的最近位姿",
    "covis：視差足夠且共享稀疏點最多者；pose：外掛的最近位姿",
    "covis: 시차가 충분하고 공유하는 희소 점이 가장 많은 것. pose: 플러그인의 가장 가까운 자세",
    "covis: die meisten gemeinsamen Sparse-Punkte bei genug Parallaxe; pose: die nächste Pose (Plugin)",
    "covis : le plus de points épars partagés avec assez de parallaxe ; pose : la pose la plus proche (greffon)",
    "covis: más puntos dispersos compartidos con suficiente paralaje; pose: la pose más cercana (complemento)",
    "covis: mais pontos esparsos partilhados com paralaxe suficiente; pose: a pose mais próxima (plugin)",
    "covis: più punti sparsi condivisi con parallasse sufficiente; pose: la posa più vicina (plugin)",
    "covis: de meeste gedeelde sparse punten bij genoeg parallax; pose: de dichtstbijzijnde pose (plug-in)",
    "covis: больше всего общих разреженных точек при достаточном параллаксе; pose: ближайшая поза (плагин)",
    "covis: yeterli paralaksla en çok ortak seyrek nokta; pose: eklentinin en yakın pozu")
SS_DENSIFY_OPT(opt_holdout,
    "Keep every Nth image out of the run entirely, for evaluation",
    "評価用に N 枚ごとに 1 枚を処理から完全に外す",
    "每隔 N 张取一张完全不参与运行，用于评估",
    "每隔 N 張取一張完全不參與執行，用於評估",
    "평가용으로 N 장마다 한 장을 실행에서 완전히 뺌",
    "Jedes N-te Bild ganz aus dem Lauf heraushalten, zur Bewertung",
    "Garder une image sur N entièrement hors de l'exécution, pour l'évaluation",
    "Dejar una de cada N imágenes totalmente fuera de la ejecución, para evaluar",
    "Deixar uma em cada N imagens totalmente fora da execução, para avaliação",
    "Tenere un'immagine ogni N del tutto fuori dall'esecuzione, per la valutazione",
    "Elk N-de beeld volledig buiten de run houden, voor evaluatie",
    "Полностью исключить каждый N-й снимок для оценки",
    "Değerlendirme için her N. görüntüyü çalışmanın tamamen dışında tut")
SS_DENSIFY_OPT(opt_split,
    "Match panoramas as six 90-degree cube faces. Default: auto",
    "パノラマを 6 枚の 90 度キューブ面としてマッチさせる。既定: auto",
    "将全景图作为六个 90 度立方体面进行匹配。默认：auto",
    "將全景圖作為六個 90 度立方體面進行匹配。預設：auto",
    "파노라마를 6 개의 90 도 큐브 면으로 매칭. 기본값: auto",
    "Panoramen als sechs 90-Grad-Würfelflächen vergleichen. Standard: auto",
    "Apparier les panoramas comme six faces de cube de 90 degrés. Défaut : auto",
    "Emparejar los panoramas como seis caras de cubo de 90 grados. Por defecto: auto",
    "Emparelhar os panoramas como seis faces de cubo de 90 graus. Por omissão: auto",
    "Accoppiare i panorami come sei facce di cubo da 90 gradi. Predefinito: auto",
    "Panorama's koppelen als zes kubusvlakken van 90 graden. Standaard: auto",
    "Сопоставлять панорамы как шесть граней куба по 90 градусов. По умолчанию: auto",
    "Panoramaları altı 90 derecelik küp yüzü olarak eşle. Varsayılan: auto")
SS_DENSIFY_OPT(opt_face_pairs,
    "auto: faces whose axes are within 60 degrees; all: every face of each neighbour",
    "auto: 軸が 60 度以内の面。all: 各近傍のすべての面",
    "auto：轴向相差 60 度以内的面；all：每个邻近图像的所有面",
    "auto：軸向相差 60 度以內的面；all：每個鄰近影像的所有面",
    "auto: 축이 60 도 이내인 면. all: 각 이웃의 모든 면",
    "auto: Flächen mit Achsen innerhalb von 60 Grad; all: jede Fläche jedes Nachbarn",
    "auto : faces dont les axes sont à moins de 60 degrés ; all : toutes les faces de chaque voisin",
    "auto: caras cuyos ejes están a menos de 60 grados; all: todas las caras de cada vecino",
    "auto: faces cujos eixos estão a menos de 60 graus; all: todas as faces de cada vizinho",
    "auto: facce con assi entro 60 gradi; all: ogni faccia di ciascun vicino",
    "auto: vlakken met assen binnen 60 graden; all: elk vlak van elke buur",
    "auto: грани с осями в пределах 60 градусов; all: все грани каждого соседа",
    "auto: eksenleri 60 derece içindeki yüzler; all: her komşunun tüm yüzleri")
SS_DENSIFY_OPT(opt_matches_per_ref,
    "Pixels drawn per reference view",
    "基準ビューごとに抽出するピクセル数",
    "每个参考视图抽取的像素数",
    "每個參考視圖抽取的像素數",
    "기준 뷰마다 뽑는 픽셀 수",
    "Pro Referenzansicht gezogene Pixel",
    "Pixels tirés par vue de référence",
    "Píxeles extraídos por vista de referencia",
    "Píxeis extraídos por vista de referência",
    "Pixel estratti per vista di riferimento",
    "Pixels getrokken per referentieaanzicht",
    "Пикселей, выбираемых на каждый опорный вид",
    "Referans görünüm başına seçilen piksel")
SS_DENSIFY_OPT(opt_min_certainty,
    "Matches less certain than this are not triangulated. Default: 0.2",
    "これより確度の低いマッチは三角測量しない。既定: 0.2",
    "确定度低于此值的匹配不做三角化。默认：0.2",
    "確定度低於此值的匹配不做三角化。預設：0.2",
    "이보다 확실성이 낮은 매칭은 삼각측량하지 않음. 기본값: 0.2",
    "Weniger sichere Zuordnungen werden nicht trianguliert. Standard: 0.2",
    "Les appariements moins sûrs que cela ne sont pas triangulés. Défaut : 0.2",
    "Las correspondencias menos seguras no se triangulan. Por defecto: 0.2",
    "As correspondências menos seguras não são trianguladas. Por omissão: 0.2",
    "Le corrispondenze meno certe non vengono triangolate. Predefinito: 0.2",
    "Minder zekere overeenkomsten worden niet getrianguleerd. Standaard: 0.2",
    "Менее уверенные соответствия не триангулируются. По умолчанию: 0.2",
    "Bundan daha az kesin eşleşmeler üçgenlenmez. Varsayılan: 0.2")
SS_DENSIFY_OPT(opt_reproj,
    "Largest reprojection error kept, in pixels at the match resolution. Default: 1",
    "残す再投影誤差の上限（マッチ解像度のピクセル）。既定: 1",
    "保留的最大重投影误差，以匹配分辨率的像素计。默认：1",
    "保留的最大重投影誤差，以匹配解析度的像素計。預設：1",
    "남기는 최대 재투영 오차, 매칭 해상도의 픽셀 단위. 기본값: 1",
    "Größter behaltener Rückprojektionsfehler, in Pixeln der Vergleichsauflösung. Standard: 1",
    "Plus grande erreur de reprojection gardée, en pixels à la résolution d'appariement. Défaut : 1",
    "Mayor error de reproyección conservado, en píxeles a la resolución de emparejamiento. Por defecto: 1",
    "Maior erro de reprojeção mantido, em píxeis à resolução de emparelhamento. Por omissão: 1",
    "Massimo errore di riproiezione tenuto, in pixel alla risoluzione di accoppiamento. Predefinito: 1",
    "Grootste behouden herprojectiefout, in pixels op de koppelresolutie. Standaard: 1",
    "Наибольшая сохраняемая ошибка перепроекции, в пикселях разрешения сопоставления. По умолчанию: 1",
    "Tutulan en büyük yeniden izdüşüm hatası, eşleme çözünürlüğünde piksel. Varsayılan: 1")
SS_DENSIFY_OPT(opt_sampson,
    "Largest Sampson error before triangulating, squared pixels at the match resolution. Default: 5",
    "三角測量前の Sampson 誤差の上限（マッチ解像度のピクセルの二乗）。既定: 5",
    "三角化前的最大 Sampson 误差，以匹配分辨率像素的平方计。默认：5",
    "三角化前的最大 Sampson 誤差，以匹配解析度像素的平方計。預設：5",
    "삼각측량 전 최대 Sampson 오차, 매칭 해상도 픽셀의 제곱. 기본값: 5",
    "Größter Sampson-Fehler vor dem Triangulieren, Quadratpixel der Vergleichsauflösung. Standard: 5",
    "Plus grande erreur de Sampson avant triangulation, pixels carrés à la résolution d'appariement. Défaut : 5",
    "Mayor error de Sampson antes de triangular, píxeles al cuadrado. Por defecto: 5",
    "Maior erro de Sampson antes de triangular, píxeis ao quadrado. Por omissão: 5",
    "Massimo errore di Sampson prima di triangolare, pixel al quadrato. Predefinito: 5",
    "Grootste Sampson-fout vóór trianguleren, vierkante pixels op de koppelresolutie. Standaard: 5",
    "Наибольшая ошибка Сэмпсона до триангуляции, квадратные пиксели. По умолчанию: 5",
    "Üçgenlemeden önceki en büyük Sampson hatası, piksel kare. Varsayılan: 5")
SS_DENSIFY_OPT(opt_parallax,
    "Smallest angle between a point's two viewing rays, in degrees. Default: 1.5",
    "点の 2 本の視線のなす角の下限（度）。既定: 1.5",
    "点的两条视线间的最小夹角（度）。默认：1.5",
    "點的兩條視線間的最小夾角（度）。預設：1.5",
    "점의 두 시선 사이의 최소 각도 (도). 기본값: 1.5",
    "Kleinster Winkel zwischen den beiden Sehstrahlen eines Punktes, in Grad. Standard: 1.5",
    "Plus petit angle entre les deux rayons de vue d'un point, en degrés. Défaut : 1.5",
    "Menor ángulo entre los dos rayos de visión de un punto, en grados. Por defecto: 1.5",
    "Menor ângulo entre os dois raios de visão de um ponto, em graus. Por omissão: 1.5",
    "Angolo minimo tra i due raggi di vista di un punto, in gradi. Predefinito: 1.5",
    "Kleinste hoek tussen de twee zichtstralen van een punt, in graden. Standaard: 1.5",
    "Наименьший угол между двумя лучами зрения точки, в градусах. По умолчанию: 1.5",
    "Bir noktanın iki görüş ışını arasındaki en küçük açı, derece. Varsayılan: 1.5")
SS_DENSIFY_OPT(opt_min_track,
    "Images a point must reproject into. Default: auto, 3, or 2 when its error is no worse than the median of the 3-image points",
    "点の再投影が合う必要がある画像数。既定: auto（3、または誤差が 3 枚の点の中央値以下なら 2）",
    "点须重投影吻合的图像数。默认：auto，即 3，或误差不超过三图像点中位数时为 2",
    "點須重投影吻合的影像數。預設：auto，即 3，或誤差不超過三影像點中位數時為 2",
    "점의 재투영이 맞아야 하는 이미지 수. 기본값: auto, 3 또는 오차가 3 장 점의 중앙값 이하이면 2",
    "Bilder, in die ein Punkt zurückprojizieren muss. Standard: auto, 3, oder 2 bei einem Fehler nicht über dem Median der 3-Bild-Punkte",
    "Images où un point doit se reprojeter. Défaut : auto, 3, ou 2 si son erreur ne dépasse pas la médiane des points à 3 images",
    "Imágenes en las que un punto debe reproyectarse. Por defecto: auto, 3, o 2 si su error no supera la mediana de los puntos de 3 imágenes",
    "Imagens em que um ponto tem de se reprojetar. Por omissão: auto, 3, ou 2 se o erro não exceder a mediana dos pontos de 3 imagens",
    "Immagini in cui un punto deve riproiettarsi. Predefinito: auto, 3, o 2 se il suo errore non supera la mediana dei punti a 3 immagini",
    "Beelden waarin een punt moet terugprojecteren. Standaard: auto, 3, of 2 als de fout niet boven de mediaan van de 3-beeldpunten ligt",
    "Снимки, в которые должна перепроецироваться точка. По умолчанию: auto, 3 или 2, если ошибка не выше медианы точек с 3 снимками",
    "Bir noktanın yeniden izdüşmesi gereken görüntüler. Varsayılan: auto, 3 ya da hatası 3 görüntülü noktaların ortancasını aşmıyorsa 2")
SS_DENSIFY_OPT(opt_covis_min_angle,
    "Neighbours must see their shared sparse points at least this many degrees apart. Default: 1.5",
    "近傍は共有する疎な点をこの角度（度）以上離れて見る必要がある。既定: 1.5",
    "邻近图像对共享稀疏点的观察夹角至少为此度数。默认：1.5",
    "鄰近影像對共享稀疏點的觀察夾角至少為此度數。預設：1.5",
    "이웃은 공유 희소 점을 이 각도(도) 이상 떨어져서 봐야 함. 기본값: 1.5",
    "Nachbarn müssen ihre gemeinsamen Sparse-Punkte mindestens so viele Grad auseinander sehen. Standard: 1.5",
    "Les voisins doivent voir leurs points épars communs sous au moins cet angle, en degrés. Défaut : 1.5",
    "Los vecinos deben ver sus puntos dispersos comunes con al menos este ángulo, en grados. Por defecto: 1.5",
    "Os vizinhos têm de ver os pontos esparsos comuns com pelo menos este ângulo, em graus. Por omissão: 1.5",
    "I vicini devono vedere i punti sparsi comuni con almeno questo angolo, in gradi. Predefinito: 1.5",
    "Buren moeten hun gedeelde sparse punten onder minstens zoveel graden zien. Standaard: 1.5",
    "Соседи должны видеть общие разреженные точки под углом не менее стольких градусов. По умолчанию: 1.5",
    "Komşular ortak seyrek noktalarını en az bu kadar derece açıyla görmeli. Varsayılan: 1.5")
SS_DENSIFY_OPT(opt_max_depth_error,
    "Drop a point whose depth moves more than this share per match pixel. Default: auto, 2% or looser on narrow captures; off",
    "マッチ 1 ピクセルで深度がこの割合以上動く点を除く。既定: auto（2%、狭い撮影ではそれより緩い）。off",
    "每匹配像素深度变化超过此比例的点被剔除。默认：auto，2%，窄基线拍摄时更宽；off",
    "每匹配像素深度變化超過此比例的點被剔除。預設：auto，2%，窄基線拍攝時更寬；off",
    "매칭 1 픽셀에 깊이가 이 비율 이상 움직이는 점은 제거. 기본값: auto, 2% (좁은 촬영에서는 더 느슨함); off",
    "Punkte verwerfen, deren Tiefe sich je Vergleichspixel um mehr als diesen Anteil ändert. Standard: auto, 2% oder lockerer bei engen Aufnahmen; off",
    "Écarter un point dont la profondeur bouge de plus de cette part par pixel d'appariement. Défaut : auto, 2 % ou moins strict sur une prise étroite ; off",
    "Descartar un punto cuya profundidad cambie más de esta fracción por píxel. Por defecto: auto, 2 % o más laxo en capturas estrechas; off",
    "Descartar um ponto cuja profundidade mude mais do que esta fração por píxel. Por omissão: auto, 2% ou mais largo em capturas estreitas; off",
    "Scartare un punto la cui profondità cambia più di questa frazione per pixel. Predefinito: auto, 2% o più largo su riprese strette; off",
    "Een punt weglaten waarvan de diepte per koppelpixel meer dan dit aandeel verschuift. Standaard: auto, 2% of ruimer bij smalle opnamen; off",
    "Отбросить точку, глубина которой на пиксель сопоставления меняется больше этой доли. По умолчанию: auto, 2% или мягче для узких съёмок; off",
    "Eşleme pikseli başına derinliği bu orandan fazla değişen noktayı at. Varsayılan: auto, %2 ya da dar çekimlerde daha gevşek; off")
SS_DENSIFY_OPT(opt_voxel,
    "One point per voxel of this size. Default: auto, half the sparse points' spacing; off",
    "このサイズのボクセルごとに 1 点。既定: auto（疎な点の間隔の半分）。off で無効",
    "每个此尺寸体素保留一个点。默认：auto，稀疏点间距的一半；off 关闭",
    "每個此尺寸體素保留一個點。預設：auto，稀疏點間距的一半；off 關閉",
    "이 크기의 복셀마다 한 점. 기본값: auto, 희소 점 간격의 절반; off 끔",
    "Ein Punkt je Voxel dieser Größe. Standard: auto, halber Abstand der Sparse-Punkte; off",
    "Un point par voxel de cette taille. Défaut : auto, la moitié de l'espacement des points épars ; off",
    "Un punto por vóxel de este tamaño. Por defecto: auto, la mitad de la separación de los puntos dispersos; off",
    "Um ponto por vóxel deste tamanho. Por omissão: auto, metade do espaçamento dos pontos esparsos; off",
    "Un punto per voxel di questa dimensione. Predefinito: auto, metà della spaziatura dei punti sparsi; off",
    "Eén punt per voxel van deze grootte. Standaard: auto, de helft van de sparse-afstand; off",
    "Одна точка на воксель такого размера. По умолчанию: auto, половина шага разреженных точек; off",
    "Bu boyuttaki voksel başına bir nokta. Varsayılan: auto, seyrek nokta aralığının yarısı; off")
SS_DENSIFY_OPT(opt_max_points,
    "Most points written; a seeded sample past it. Default: auto; off",
    "書き出す点の上限。超えたらシード付きで抽出。既定: auto。off で無効",
    "最多写入的点数，超出时按种子抽样。默认：auto；off 关闭",
    "最多寫入的點數，超出時按種子抽樣。預設：auto；off 關閉",
    "쓰는 점의 최대 개수. 넘으면 시드로 표본 추출. 기본값: auto; off 끔",
    "Höchstens so viele Punkte; darüber eine Stichprobe mit festem Seed. Standard: auto; off",
    "Nombre maximal de points écrits ; au-delà, un tirage à graine fixe. Défaut : auto ; off",
    "Máximo de puntos escritos; por encima, una muestra con semilla. Por defecto: auto; off",
    "Máximo de pontos escritos; acima disso, uma amostra com semente. Por omissão: auto; off",
    "Massimo di punti scritti; oltre, un campione con seme. Predefinito: auto; off",
    "Hoogstens zoveel punten; daarboven een steekproef met vaste seed. Standaard: auto; off",
    "Наибольшее число точек; сверх него выборка с зерном. По умолчанию: auto; off",
    "Yazılan en fazla nokta; üstünde tohumlu örnek. Varsayılan: auto; off")
SS_DENSIFY_OPT(opt_max_baseline,
    "Farthest apart two matched images may be. Default: auto (metric models only); off",
    "マッチさせる 2 枚の画像の最大距離。既定: auto（メートル単位のモデルのみ）。off",
    "两张匹配图像之间的最大距离。默认：auto（仅公制模型）；off",
    "兩張匹配影像之間的最大距離。預設：auto（僅公制模型）；off",
    "매칭하는 두 이미지 사이의 최대 거리. 기본값: auto (미터 모델만); off",
    "Größter Abstand zweier verglichener Bilder. Standard: auto (nur metrische Modelle); off",
    "Distance maximale entre deux images appariées. Défaut : auto (modèles métriques seulement) ; off",
    "Distancia máxima entre dos imágenes emparejadas. Por defecto: auto (solo modelos métricos); off",
    "Distância máxima entre duas imagens emparelhadas. Por omissão: auto (só modelos métricos); off",
    "Distanza massima tra due immagini accoppiate. Predefinito: auto (solo modelli metrici); off",
    "Grootste afstand tussen twee gekoppelde beelden. Standaard: auto (alleen metrische modellen); off",
    "Наибольшее расстояние между двумя сопоставляемыми снимками. По умолчанию: auto (только метрические); off",
    "Eşlenen iki görüntü arasındaki en büyük uzaklık. Varsayılan: auto (yalnızca metrik modeller); off")
SS_DENSIFY_OPT(opt_seed,
    "Seed for the sampling and the point cap. Default: 0",
    "抽出と点数上限のシード。既定: 0",
    "抽样与点数上限的随机种子。默认：0",
    "抽樣與點數上限的隨機種子。預設：0",
    "표본 추출과 점 상한의 시드. 기본값: 0",
    "Seed für die Stichprobe und die Punktobergrenze. Standard: 0",
    "Graine du tirage et du plafond de points. Défaut : 0",
    "Semilla del muestreo y del límite de puntos. Por defecto: 0",
    "Semente da amostragem e do limite de pontos. Por omissão: 0",
    "Seme del campionamento e del limite di punti. Predefinito: 0",
    "Seed voor de steekproef en de puntgrens. Standaard: 0",
    "Зерно выборки и ограничения числа точек. По умолчанию: 0",
    "Örnekleme ve nokta sınırı için tohum. Varsayılan: 0")
SS_DENSIFY_OPT(opt_accept_license,
    "Accept Meta's DINOv3 License and RoMa v2's MIT terms after reading them; the checkpoint carries both",
    "Meta の DINOv3 ライセンスと RoMa v2 の MIT 条項を読んだうえで受け入れる。チェックポイントは両方を含む",
    "阅读后接受 Meta 的 DINOv3 许可与 RoMa v2 的 MIT 条款；检查点同时包含两者",
    "閱讀後接受 Meta 的 DINOv3 授權與 RoMa v2 的 MIT 條款；檢查點同時包含兩者",
    "Meta 의 DINOv3 라이선스와 RoMa v2 의 MIT 조항을 읽은 뒤 수락. 체크포인트에 둘 다 들어 있음",
    "Metas DINOv3-Lizenz und die MIT-Bedingungen von RoMa v2 nach dem Lesen annehmen; der Checkpoint enthält beide",
    "Accepter la licence DINOv3 de Meta et les termes MIT de RoMa v2 après lecture ; le point de contrôle contient les deux",
    "Aceptar la licencia DINOv3 de Meta y los términos MIT de RoMa v2 tras leerlos; el punto de control contiene ambos",
    "Aceitar a licença DINOv3 da Meta e os termos MIT do RoMa v2 depois de os ler; o ponto de controlo contém ambos",
    "Accettare la licenza DINOv3 di Meta e i termini MIT di RoMa v2 dopo averli letti; il checkpoint li contiene entrambi",
    "Meta's DINOv3-licentie en de MIT-voorwaarden van RoMa v2 aanvaarden na lezing; het checkpoint bevat beide",
    "Принять лицензию DINOv3 от Meta и условия MIT RoMa v2 после прочтения; контрольная точка содержит обе",
    "Okuduktan sonra Meta'nın DINOv3 Lisansını ve RoMa v2'nin MIT koşullarını kabul et; denetim noktası ikisini de içerir")
SS_DENSIFY_OPT(opt_overwrite,
    "Replace an output model that already exists",
    "既にある出力モデルを置き換える",
    "替换已存在的输出模型",
    "取代已存在的輸出模型",
    "이미 있는 출력 모델을 바꿈",
    "Ein vorhandenes Ausgabemodell ersetzen",
    "Remplacer un modèle de sortie existant",
    "Reemplazar un modelo de salida existente",
    "Substituir um modelo de saída existente",
    "Sostituire un modello di uscita esistente",
    "Een bestaand uitvoermodel vervangen",
    "Заменить уже существующую выходную модель",
    "Var olan bir çıktı modelini değiştir")
SS_DENSIFY_OPT(opt_force,
    "Run even when the masks look inverted",
    "マスクが反転しているように見えても実行する",
    "即使蒙版看起来是反的也运行",
    "即使遮罩看起來是反的也執行",
    "마스크가 반전된 것처럼 보여도 실행",
    "Auch laufen, wenn die Masken invertiert aussehen",
    "S'exécuter même si les masques semblent inversés",
    "Ejecutar aunque las máscaras parezcan invertidas",
    "Executar mesmo que as máscaras pareçam invertidas",
    "Eseguire anche se le maschere sembrano invertite",
    "Ook draaien als de maskers omgekeerd lijken",
    "Запускать, даже если маски выглядят инвертированными",
    "Maskeler ters görünse bile çalıştır")

#undef SS_DENSIFY_OPT

SS_MSG(label_common,
    EN("Also:"), JA("ほかに:"), ZH_HANS("另外："), ZH_HANT("另外："), KO("그 밖에:"),
    DE("Außerdem:"), FR("Aussi :"), ES("También:"), PT("Também:"), IT("Inoltre:"),
    NL("Ook:"), RU("Также:"), TR("Ayrıca:"));

SS_MSG(model,
    EN("Model {0}: images {1}, sparse points {2}, median spacing {3}"),
    JA("モデル {0}: 画像 {1}、疎な点 {2}、間隔の中央値 {3}"),
    ZH_HANS("模型 {0}：图像 {1}，稀疏点 {2}，间距中位数 {3}"),
    ZH_HANT("模型 {0}：影像 {1}，稀疏點 {2}，間距中位數 {3}"),
    KO("모델 {0}: 이미지 {1}, 희소 점 {2}, 간격 중앙값 {3}"),
    DE("Modell {0}: Bilder {1}, Sparse-Punkte {2}, mittlerer Abstand {3}"),
    FR("Modèle {0} : images {1}, points épars {2}, espacement médian {3}"),
    ES("Modelo {0}: imágenes {1}, puntos dispersos {2}, separación mediana {3}"),
    PT("Modelo {0}: imagens {1}, pontos esparsos {2}, espaçamento mediano {3}"),
    IT("Modello {0}: immagini {1}, punti sparsi {2}, spaziatura mediana {3}"),
    NL("Model {0}: beelden {1}, sparse punten {2}, mediane afstand {3}"),
    RU("Модель {0}: снимков {1}, разреженных точек {2}, медианный шаг {3}"),
    TR("Model {0}: görüntü {1}, seyrek nokta {2}, ortanca aralık {3}"));

SS_MSG(held_out,
    EN("Held out, never matched (every {0}th image): {1}"),
    JA("除外して一切マッチさせない画像（{0} 枚ごと）: {1}"),
    ZH_HANS("留出且从不匹配（每 {0} 张取一张）：{1}"),
    ZH_HANT("留出且從不匹配（每 {0} 張取一張）：{1}"),
    KO("제외하여 매칭하지 않음 ({0} 장마다): {1}"),
    DE("Zurückgehalten, nie verglichen (jedes {0}. Bild): {1}"),
    FR("Mises de côté, jamais appariées (une image sur {0}) : {1}"),
    ES("Apartadas, nunca emparejadas (una de cada {0}): {1}"),
    PT("Postas de parte, nunca emparelhadas (uma em cada {0}): {1}"),
    IT("Tenute fuori, mai accoppiate (una ogni {0}): {1}"),
    NL("Achtergehouden, nooit gekoppeld (elk {0}e beeld): {1}"),
    RU("Отложены и не сопоставляются (каждый {0}-й): {1}"),
    TR("Ayrıldı, hiç eşlenmez (her {0}. görüntü): {1}"));

SS_MSG(faces,
    EN("Panoramas are matched as 90-degree cube faces; native face size {0} px"),
    JA("パノラマは 90 度のキューブ面としてマッチさせます。面の元の大きさ {0} px"),
    ZH_HANS("全景图按 90 度立方体面进行匹配；面的原始尺寸 {0} px"),
    ZH_HANT("全景圖按 90 度立方體面進行匹配；面的原始尺寸 {0} px"),
    KO("파노라마는 90 도 큐브 면으로 매칭합니다. 면의 원래 크기 {0} px"),
    DE("Panoramen werden als 90-Grad-Würfelflächen verglichen; native Flächengröße {0} px"),
    FR("Les panoramas sont appariés comme faces de cube de 90 degrés ; taille native d'une face {0} px"),
    ES("Los panoramas se emparejan como caras de cubo de 90 grados; tamaño nativo de cara {0} px"),
    PT("Os panoramas são emparelhados como faces de cubo de 90 graus; tamanho nativo da face {0} px"),
    IT("I panorami sono accoppiati come facce di cubo da 90 gradi; dimensione nativa {0} px"),
    NL("Panorama's worden gekoppeld als kubusvlakken van 90 graden; eigen vlakgrootte {0} px"),
    RU("Панорамы сопоставляются как грани куба по 90 градусов; исходный размер грани {0} px"),
    TR("Panoramalar 90 derecelik küp yüzleri olarak eşlenir; özgün yüz boyutu {0} px"));

SS_MSG(selection,
    EN("References: {0} of {1} images, reference views {2}, pairs {3}; neighbours: {4}, by {5}"),
    JA("基準画像: {1} 枚中 {0}、基準ビュー {2}、ペア {3}。近傍: {4}（{5} で選択）"),
    ZH_HANS("参考图像：{1} 张中 {0} 张，参考视图 {2}，图像对 {3}；邻近：{4}，按 {5} 选择"),
    ZH_HANT("參考影像：{1} 張中 {0} 張，參考視圖 {2}，影像對 {3}；鄰近：{4}，按 {5} 選擇"),
    KO("기준 이미지: {1} 장 중 {0}, 기준 뷰 {2}, 쌍 {3}; 이웃: {4}, {5} 기준"),
    DE("Referenzen: {0} von {1} Bildern, Referenzansichten {2}, Paare {3}; Nachbarn: {4}, nach {5}"),
    FR("Références : {0} sur {1} images, vues de référence {2}, paires {3} ; voisins : {4}, par {5}"),
    ES("Referencias: {0} de {1} imágenes, vistas de referencia {2}, pares {3}; vecinos: {4}, por {5}"),
    PT("Referências: {0} de {1} imagens, vistas de referência {2}, pares {3}; vizinhos: {4}, por {5}"),
    IT("Riferimenti: {0} di {1} immagini, viste di riferimento {2}, coppie {3}; vicini: {4}, per {5}"),
    NL("Referenties: {0} van {1} beelden, referentieaanzichten {2}, paren {3}; buren: {4}, op {5}"),
    RU("Опорные: {0} из {1} снимков, опорных видов {2}, пар {3}; соседей: {4}, по {5}"),
    TR("Referanslar: {1} görüntüden {0}, referans görünüm {2}, çift {3}; komşu: {4}, {5} ile"));

SS_MSG(baseline,
    EN("Neighbours at most {0} apart: three times the median, on a metric model"),
    JA("近傍の距離は最大 {0}: メートル単位のモデルで中央値の 3 倍"),
    ZH_HANS("邻近图像最远相距 {0}：公制模型上取中位数的三倍"),
    ZH_HANT("鄰近影像最遠相距 {0}：公制模型上取中位數的三倍"),
    KO("이웃 사이 거리는 최대 {0}: 미터 모델에서 중앙값의 세 배"),
    DE("Nachbarn höchstens {0} entfernt: das Dreifache des Medians, bei einem metrischen Modell"),
    FR("Voisins à au plus {0} : trois fois la médiane, sur un modèle métrique"),
    ES("Vecinos a lo sumo a {0}: tres veces la mediana, en un modelo métrico"),
    PT("Vizinhos no máximo a {0}: três vezes a mediana, num modelo métrico"),
    IT("Vicini al massimo a {0}: tre volte la mediana, su un modello metrico"),
    NL("Buren hoogstens {0} uit elkaar: driemaal de mediaan, bij een metrisch model"),
    RU("Соседи не дальше {0}: три медианы, для метрической модели"),
    TR("Komşular en fazla {0} uzakta: metrik modelde ortancanın üç katı"));

SS_MSG(filters,
    EN("Filters at the {0} px match size: reprojection {1} px, Sampson {2} px², parallax {3}°, "
       "certainty {4}, track {5} images"),
    JA("マッチ解像度 {0} px での判定: 再投影 {1} px、Sampson {2} px²、視差 {3}°、確度 {4}、"
       "トラック {5} 枚"),
    ZH_HANS("在 {0} px 匹配尺寸下的筛选：重投影 {1} px，Sampson {2} px²，视差 {3}°，确定度 {4}，"
            "轨迹 {5} 张图像"),
    ZH_HANT("在 {0} px 匹配尺寸下的篩選：重投影 {1} px，Sampson {2} px²，視差 {3}°，確定度 {4}，"
            "軌跡 {5} 張影像"),
    KO("{0} px 매칭 크기 기준 필터: 재투영 {1} px, Sampson {2} px², 시차 {3}°, 확실성 {4}, "
       "트랙 {5} 장"),
    DE("Filter bei {0} px Vergleichsgröße: Rückprojektion {1} px, Sampson {2} px², Parallaxe {3}°, "
       "Sicherheit {4}, Spur {5} Bilder"),
    FR("Filtres à la taille d'appariement {0} px : reprojection {1} px, Sampson {2} px², parallaxe "
       "{3}°, certitude {4}, piste {5} images"),
    ES("Filtros al tamaño de emparejamiento {0} px: reproyección {1} px, Sampson {2} px², paralaje "
       "{3}°, certeza {4}, pista {5} imágenes"),
    PT("Filtros ao tamanho de emparelhamento {0} px: reprojeção {1} px, Sampson {2} px², paralaxe "
       "{3}°, certeza {4}, trilho {5} imagens"),
    IT("Filtri alla dimensione di accoppiamento {0} px: riproiezione {1} px, Sampson {2} px², "
       "parallasse {3}°, certezza {4}, traccia {5} immagini"),
    NL("Filters bij {0} px koppelgrootte: herprojectie {1} px, Sampson {2} px², parallax {3}°, "
       "zekerheid {4}, spoor {5} beelden"),
    RU("Фильтры при размере сопоставления {0} px: перепроекция {1} px, Сэмпсон {2} px², параллакс "
       "{3}°, уверенность {4}, трек {5} снимков"),
    TR("{0} px eşleme boyutunda süzgeçler: yeniden izdüşüm {1} px, Sampson {2} px², paralaks {3}°, "
       "kesinlik {4}, iz {5} görüntü"));

SS_MSG(budget,
    EN("Samples per view {0}; voxel {1}; points at most {2}"),
    JA("ビューあたりのサンプル {0}。ボクセル {1}。点の上限 {2}"),
    ZH_HANS("每视图样本 {0}；体素 {1}；点数上限 {2}"),
    ZH_HANT("每視圖樣本 {0}；體素 {1}；點數上限 {2}"),
    KO("뷰당 표본 {0}; 복셀 {1}; 점 상한 {2}"),
    DE("Stichproben je Ansicht {0}; Voxel {1}; höchstens Punkte {2}"),
    FR("Échantillons par vue {0} ; voxel {1} ; points au plus {2}"),
    ES("Muestras por vista {0}; vóxel {1}; puntos como máximo {2}"),
    PT("Amostras por vista {0}; vóxel {1}; pontos no máximo {2}"),
    IT("Campioni per vista {0}; voxel {1}; punti al massimo {2}"),
    NL("Steekproeven per aanzicht {0}; voxel {1}; punten hoogstens {2}"),
    RU("Выборок на вид {0}; воксель {1}; точек не более {2}"),
    TR("Görünüm başına örnek {0}; voksel {1}; en fazla nokta {2}"));

SS_MSG(masks,
    EN("Masks: kept on average {0}, over sampled masks {1}"),
    JA("マスク: 残る割合の平均 {0}（調べたマスク {1}）"),
    ZH_HANS("蒙版：平均保留 {0}，抽查蒙版 {1}"),
    ZH_HANT("遮罩：平均保留 {0}，抽查遮罩 {1}"),
    KO("마스크: 평균 보존 {0}, 표본 마스크 {1}"),
    DE("Masken: im Mittel behalten {0}, geprüfte Masken {1}"),
    FR("Masques : gardé en moyenne {0}, masques échantillonnés {1}"),
    ES("Máscaras: conservado de media {0}, máscaras muestreadas {1}"),
    PT("Máscaras: mantido em média {0}, máscaras amostradas {1}"),
    IT("Maschere: tenuto in media {0}, maschere campionate {1}"),
    NL("Maskers: gemiddeld behouden {0}, steekproefmaskers {1}"),
    RU("Маски: в среднем сохраняется {0}, проверено масок {1}"),
    TR("Maskeler: ortalama tutulan {0}, örneklenen maske {1}"));

SS_MSG(masks_inverted,
    EN("The masks keep {0} of each image on average, which looks inverted. Masks here are white "
       "where the image is kept: pass --flip-mask for masks that paint what to remove, or --force."),
    JA("マスクが残すのは各画像の平均 {0} で、反転しているように見えます。ここでのマスクは残す所が"
       "白です。取り除く所を塗ったマスクなら --flip-mask を、そのまま使うなら --force を指定してください。"),
    ZH_HANS("蒙版平均只保留每张图像的 {0}，看起来是反的。这里的蒙版以白色表示保留区域：若蒙版标出的是"
            "要去除的区域，请加 --flip-mask，或用 --force 强制运行。"),
    ZH_HANT("遮罩平均只保留每張影像的 {0}，看起來是反的。這裡的遮罩以白色表示保留區域：若遮罩標出的是"
            "要去除的區域，請加 --flip-mask，或用 --force 強制執行。"),
    KO("마스크가 각 이미지의 평균 {0} 만 남겨 반전된 것처럼 보입니다. 여기서 마스크는 남기는 곳이 "
       "흰색입니다. 제거할 곳을 칠한 마스크라면 --flip-mask 를, 그대로 쓰려면 --force 를 주세요."),
    DE("Die Masken behalten im Mittel {0} jedes Bildes, was invertiert aussieht. Masken sind hier "
       "weiß, wo das Bild bleibt: --flip-mask für Masken, die das zu Entfernende malen, oder --force."),
    FR("Les masques gardent en moyenne {0} de chaque image, ce qui semble inversé. Ici les masques "
       "sont blancs là où l'image est gardée : --flip-mask pour des masques qui peignent ce qu'il faut "
       "retirer, ou --force."),
    ES("Las máscaras conservan de media {0} de cada imagen, lo que parece invertido. Aquí las "
       "máscaras son blancas donde se conserva la imagen: usa --flip-mask para máscaras que pintan "
       "lo que se quita, o --force."),
    PT("As máscaras mantêm em média {0} de cada imagem, o que parece invertido. Aqui as máscaras são "
       "brancas onde a imagem é mantida: use --flip-mask para máscaras que pintam o que remover, ou "
       "--force."),
    IT("Le maschere tengono in media {0} di ogni immagine, il che sembra invertito. Qui le maschere "
       "sono bianche dove l'immagine resta: usa --flip-mask per maschere che dipingono ciò da togliere, "
       "o --force."),
    NL("De maskers behouden gemiddeld {0} van elk beeld, wat omgekeerd lijkt. Maskers zijn hier wit "
       "waar het beeld blijft: gebruik --flip-mask voor maskers die tekenen wat weg moet, of --force."),
    RU("Маски сохраняют в среднем {0} каждого снимка, это похоже на инверсию. Здесь маски белые там, "
       "где снимок сохраняется: укажите --flip-mask для масок, закрашивающих удаляемое, или --force."),
    TR("Maskeler her görüntünün ortalama {0} kadarını tutuyor; bu ters görünüyor. Burada maskeler "
       "görüntünün tutulduğu yerde beyazdır: kaldırılacak yeri boyayan maskeler için --flip-mask, ya "
       "da --force verin."));

SS_MSG(warp_scale,
    EN("The matcher returns {0} px warps for {1} px inputs: its pixels are coarser, so the "
       "pixel thresholds are applied in warp pixels, scaled from the {1} px values"),
    JA("マッチャは {1} px の入力に {0} px のワープを返します。ピクセルが粗いため、ピクセルのしきい値は "
       "{1} px の値から換算してワープのピクセルで適用します"),
    ZH_HANS("匹配器对 {1} px 输入返回 {0} px 的形变场：其像素更粗，像素阈值按 {1} px 的值换算后在形变像素上应用"),
    ZH_HANT("匹配器對 {1} px 輸入傳回 {0} px 的形變場：其像素更粗，像素閾值按 {1} px 的值換算後在形變像素上套用"),
    KO("매처가 {1} px 입력에 {0} px 워프를 돌려줍니다. 픽셀이 더 거칠어 픽셀 임계값은 {1} px 값에서 "
       "환산해 워프 픽셀로 적용합니다"),
    DE("Der Zuordner liefert {0}-px-Warps für {1}-px-Eingaben: seine Pixel sind gröber, daher gelten die "
       "Pixelschwellen in Warp-Pixeln, umgerechnet aus den {1}-px-Werten"),
    FR("L'apparieur renvoie des champs de {0} px pour des entrées de {1} px : ses pixels sont plus "
       "grossiers, les seuils en pixels s'appliquent donc en pixels du champ, convertis depuis {1} px"),
    ES("El emparejador devuelve campos de {0} px para entradas de {1} px: sus píxeles son más gruesos, "
       "así que los umbrales se aplican en píxeles del campo, convertidos desde {1} px"),
    PT("O emparelhador devolve campos de {0} px para entradas de {1} px: os píxeis são mais grossos, por "
       "isso os limiares aplicam-se em píxeis do campo, convertidos de {1} px"),
    IT("L'accoppiatore restituisce campi da {0} px per ingressi da {1} px: i suoi pixel sono più "
       "grossi, quindi le soglie si applicano in pixel del campo, convertite da {1} px"),
    NL("De koppelaar geeft velden van {0} px voor invoer van {1} px: zijn pixels zijn grover, dus de "
       "pixeldrempels gelden in veldpixels, omgerekend vanaf {1} px"),
    RU("Сопоставитель возвращает поля {0} px для входа {1} px: его пиксели крупнее, поэтому пороги "
       "применяются в пикселях поля, пересчитанные из значений для {1} px"),
    TR("Eşleyici {1} px girdiler için {0} px alanlar döndürüyor: pikselleri daha kaba, bu yüzden piksel "
       "eşikleri {1} px değerlerinden çevrilerek alan piksellerinde uygulanır"));

SS_MSG(matcher,
    EN("Matcher: {0}"), JA("マッチャ: {0}"), ZH_HANS("匹配器：{0}"), ZH_HANT("匹配器：{0}"),
    KO("매처: {0}"), DE("Zuordner: {0}"), FR("Apparieur : {0}"), ES("Emparejador: {0}"),
    PT("Emparelhador: {0}"), IT("Accoppiatore: {0}"), NL("Koppelaar: {0}"),
    RU("Сопоставитель: {0}"), TR("Eşleyici: {0}"));

SS_MSG(progress,
    EN("Reference view {0} of {1}: points so far {2}"),
    JA("基準ビュー {1} 件中 {0}: これまでの点 {2}"),
    ZH_HANS("参考视图 {0}/{1}：目前点数 {2}"),
    ZH_HANT("參考視圖 {0}/{1}：目前點數 {2}"),
    KO("기준 뷰 {1} 중 {0}: 지금까지 점 {2}"),
    DE("Referenzansicht {0} von {1}: bisherige Punkte {2}"),
    FR("Vue de référence {0} sur {1} : points jusqu'ici {2}"),
    ES("Vista de referencia {0} de {1}: puntos hasta ahora {2}"),
    PT("Vista de referência {0} de {1}: pontos até agora {2}"),
    IT("Vista di riferimento {0} di {1}: punti finora {2}"),
    NL("Referentieaanzicht {0} van {1}: punten tot nu toe {2}"),
    RU("Опорный вид {0} из {1}: точек пока {2}"),
    TR("Referans görünüm {0} / {1}: şimdiye kadar nokta {2}"));

SS_MSG(exported,
    EN("Views and pairs to match: {0}; pairs {1}"),
    JA("マッチさせるビューとペア: {0}。ペア {1}"),
    ZH_HANS("待匹配的视图与图像对：{0}；图像对 {1}"),
    ZH_HANT("待匹配的視圖與影像對：{0}；影像對 {1}"),
    KO("매칭할 뷰와 쌍: {0}; 쌍 {1}"),
    DE("Zu vergleichende Ansichten und Paare: {0}; Paare {1}"),
    FR("Vues et paires à apparier : {0} ; paires {1}"),
    ES("Vistas y pares a emparejar: {0}; pares {1}"),
    PT("Vistas e pares a emparelhar: {0}; pares {1}"),
    IT("Viste e coppie da accoppiare: {0}; coppie {1}"),
    NL("Te koppelen aanzichten en paren: {0}; paren {1}"),
    RU("Виды и пары для сопоставления: {0}; пар {1}"),
    TR("Eşlenecek görünümler ve çiftler: {0}; çift {1}"));

SS_MSG(rejected,
    EN("Samples {0}. Rejected: low certainty {1}, outside the image {2}, Sampson {3}, "
       "reprojection {4}, behind a camera {5}, parallax {6}, short track {7}; merged in a voxel {8}"),
    JA("サンプル {0}。除外: 確度不足 {1}、画像外 {2}、Sampson {3}、再投影 {4}、カメラの後ろ {5}、"
       "視差 {6}、短いトラック {7}。ボクセルで統合 {8}"),
    ZH_HANS("样本 {0}。剔除：确定度低 {1}，落在图像外 {2}，Sampson {3}，重投影 {4}，位于相机后方 {5}，"
            "视差 {6}，轨迹过短 {7}；体素内合并 {8}"),
    ZH_HANT("樣本 {0}。剔除：確定度低 {1}，落在影像外 {2}，Sampson {3}，重投影 {4}，位於相機後方 {5}，"
            "視差 {6}，軌跡過短 {7}；體素內合併 {8}"),
    KO("표본 {0}. 제외: 낮은 확실성 {1}, 이미지 밖 {2}, Sampson {3}, 재투영 {4}, 카메라 뒤 {5}, "
       "시차 {6}, 짧은 트랙 {7}; 복셀에서 합침 {8}"),
    DE("Stichproben {0}. Verworfen: geringe Sicherheit {1}, außerhalb des Bildes {2}, Sampson {3}, "
       "Rückprojektion {4}, hinter einer Kamera {5}, Parallaxe {6}, kurze Spur {7}; im Voxel vereint {8}"),
    FR("Échantillons {0}. Rejetés : certitude faible {1}, hors de l'image {2}, Sampson {3}, "
       "reprojection {4}, derrière une caméra {5}, parallaxe {6}, piste courte {7} ; fusionnés dans un voxel {8}"),
    ES("Muestras {0}. Rechazadas: poca certeza {1}, fuera de la imagen {2}, Sampson {3}, "
       "reproyección {4}, detrás de una cámara {5}, paralaje {6}, pista corta {7}; fusionadas en un vóxel {8}"),
    PT("Amostras {0}. Rejeitadas: pouca certeza {1}, fora da imagem {2}, Sampson {3}, reprojeção {4}, "
       "atrás de uma câmara {5}, paralaxe {6}, trilho curto {7}; fundidas num vóxel {8}"),
    IT("Campioni {0}. Scartati: certezza bassa {1}, fuori dall'immagine {2}, Sampson {3}, "
       "riproiezione {4}, dietro una camera {5}, parallasse {6}, traccia corta {7}; uniti in un voxel {8}"),
    NL("Steekproeven {0}. Afgewezen: lage zekerheid {1}, buiten het beeld {2}, Sampson {3}, "
       "herprojectie {4}, achter een camera {5}, parallax {6}, kort spoor {7}; samengevoegd in een voxel {8}"),
    RU("Выборок {0}. Отброшено: низкая уверенность {1}, вне снимка {2}, Сэмпсон {3}, перепроекция "
       "{4}, за камерой {5}, параллакс {6}, короткий трек {7}; объединено в вокселе {8}"),
    TR("Örnek {0}. Elenen: düşük kesinlik {1}, görüntü dışı {2}, Sampson {3}, yeniden izdüşüm {4}, "
       "kamera arkası {5}, paralaks {6}, kısa iz {7}; vokselde birleşen {8}"));

SS_MSG(done,
    EN("Dense model written: {0}; points {1}; time {2}, of which matching {3}"),
    JA("密なモデルを書き出しました: {0}。点 {1}。時間 {2}（うちマッチ {3}）"),
    ZH_HANS("已写入稠密模型：{0}；点数 {1}；用时 {2}，其中匹配 {3}"),
    ZH_HANT("已寫入稠密模型：{0}；點數 {1}；用時 {2}，其中匹配 {3}"),
    KO("조밀한 모델을 썼습니다: {0}; 점 {1}; 시간 {2}, 그중 매칭 {3}"),
    DE("Dichtes Modell geschrieben: {0}; Punkte {1}; Zeit {2}, davon Zuordnung {3}"),
    FR("Modèle dense écrit : {0} ; points {1} ; durée {2}, dont appariement {3}"),
    ES("Modelo denso escrito: {0}; puntos {1}; tiempo {2}, de ellos emparejamiento {3}"),
    PT("Modelo denso escrito: {0}; pontos {1}; tempo {2}, dos quais emparelhamento {3}"),
    IT("Modello denso scritto: {0}; punti {1}; tempo {2}, di cui accoppiamento {3}"),
    NL("Dicht model geschreven: {0}; punten {1}; tijd {2}, waarvan koppelen {3}"),
    RU("Плотная модель записана: {0}; точек {1}; время {2}, из них сопоставление {3}"),
    TR("Yoğun model yazıldı: {0}; nokta {1}; süre {2}, bunun eşleme {3}"));

SS_MSG(out_exists,
    EN("{0} already exists: pass --overwrite to replace it"),
    JA("{0} は既にあります。置き換えるには --overwrite を指定してください"),
    ZH_HANS("{0} 已存在：如需替换请加 --overwrite"),
    ZH_HANT("{0} 已存在：如需取代請加 --overwrite"),
    KO("{0} 이(가) 이미 있습니다: 바꾸려면 --overwrite 를 주세요"),
    DE("{0} existiert bereits: --overwrite ersetzt es"),
    FR("{0} existe déjà : passez --overwrite pour le remplacer"),
    ES("{0} ya existe: usa --overwrite para reemplazarlo"),
    PT("{0} já existe: use --overwrite para o substituir"),
    IT("{0} esiste già: usa --overwrite per sostituirlo"),
    NL("{0} bestaat al: geef --overwrite om het te vervangen"),
    RU("{0} уже существует: укажите --overwrite, чтобы заменить"),
    TR("{0} zaten var: değiştirmek için --overwrite verin"));

SS_MSG(error,
    EN("densify: {0}"), JA("densify: {0}"), ZH_HANS("densify：{0}"), ZH_HANT("densify：{0}"),
    KO("densify: {0}"), DE("densify: {0}"), FR("densify : {0}"), ES("densify: {0}"),
    PT("densify: {0}"), IT("densify: {0}"), NL("densify: {0}"), RU("densify: {0}"),
    TR("densify: {0}"));

SS_MSG(bad_value,
    EN("{0}: '{1}' is not a value it takes"),
    JA("{0}: '{1}' は受け付けない値です"),
    ZH_HANS("{0}：不接受取值 '{1}'"),
    ZH_HANT("{0}：不接受取值 '{1}'"),
    KO("{0}: '{1}' 은(는) 받지 않는 값입니다"),
    DE("{0}: '{1}' ist kein zulässiger Wert"),
    FR("{0} : '{1}' n'est pas une valeur acceptée"),
    ES("{0}: '{1}' no es un valor válido"),
    PT("{0}: '{1}' não é um valor aceite"),
    IT("{0}: '{1}' non è un valore accettato"),
    NL("{0}: '{1}' is geen geldige waarde"),
    RU("{0}: значение '{1}' не подходит"),
    TR("{0}: '{1}' kabul edilen bir değer değil"));

SS_MSG(unknown_option,
    EN("Unknown option '{0}'; --help lists them"),
    JA("不明なオプション '{0}'。一覧は --help で"),
    ZH_HANS("未知选项 '{0}'；--help 列出所有选项"),
    ZH_HANT("未知選項 '{0}'；--help 列出所有選項"),
    KO("알 수 없는 옵션 '{0}'; 목록은 --help"),
    DE("Unbekannte Option '{0}'; --help listet sie auf"),
    FR("Option inconnue '{0}' ; --help les liste"),
    ES("Opción desconocida '{0}'; --help las muestra"),
    PT("Opção desconhecida '{0}'; --help lista-as"),
    IT("Opzione sconosciuta '{0}'; --help le elenca"),
    NL("Onbekende optie '{0}'; --help somt ze op"),
    RU("Неизвестный параметр '{0}'; список в --help"),
    TR("Bilinmeyen seçenek '{0}'; --help hepsini listeler"));

}  // namespace densify
}  // namespace msg
}  // namespace i18n
}  // namespace spirula

#include "i18n/EndCatalog.h"
