#pragma once

// The mask editor (app/gui/mask/): its tools' modes, its status strip, its
// errors and the two log lines the dataset run prints for it. Same two rules
// as Edit.h: counts are labelled, never inflected; keys stay Latin.

#include "i18n/BeginCatalog.h"

namespace spirula {
namespace i18n {
namespace msg {
namespace maskedit {

// ===========================================================================
// History labels
// ===========================================================================

SS_MSG(op_drop,
    EN("Force drop"),
    JA("強制除外"),
    ZH_HANS("强制丢弃"),
    ZH_HANT("強制捨棄"),
    KO("강제 제외"),
    DE("Erzwungen verwerfen"),
    FR("Exclusion forcée"),
    ES("Descarte forzado"),
    PT("Descarte forçado"),
    IT("Scarto forzato"),
    NL("Geforceerd weglaten"),
    RU("Принудительно убрать"),
    TR("Zorla at"));

SS_MSG(op_keep,
    EN("Force keep"),
    JA("強制保持"),
    ZH_HANS("强制保留"),
    ZH_HANT("強制保留"),
    KO("강제 유지"),
    DE("Erzwungen behalten"),
    FR("Conservation forcée"),
    ES("Conservación forzada"),
    PT("Manutenção forçada"),
    IT("Mantenimento forzato"),
    NL("Geforceerd behouden"),
    RU("Принудительно оставить"),
    TR("Zorla tut"));

SS_MSG(op_clear,
    EN("Clear correction"),
    JA("修正を消去"),
    ZH_HANS("清除修正"),
    ZH_HANT("清除修正"),
    KO("수정 지우기"),
    DE("Korrektur löschen"),
    FR("Effacer la correction"),
    ES("Borrar corrección"),
    PT("Apagar correção"),
    IT("Cancella correzione"),
    NL("Correctie wissen"),
    RU("Стереть исправление"),
    TR("Düzeltmeyi sil"));

// ===========================================================================
// Entry, window, actions
// ===========================================================================

SS_MSG(correct_masks,
    EN("Correct masks"),
    JA("マスクを修正"),
    ZH_HANS("修正蒙版"),
    ZH_HANT("修正遮罩"),
    KO("마스크 수정"),
    DE("Masken korrigieren"),
    FR("Corriger les masques"),
    ES("Corregir máscaras"),
    PT("Corrigir máscaras"),
    IT("Correggi maschere"),
    NL("Maskers corrigeren"),
    RU("Исправить маски"),
    TR("Maskeleri düzelt"));

SS_MSG(correct_masks_help,
    EN("Paint corrections onto the masks a run wrote. They are kept in mask_edits/ beside the dataset and re-applied when masking runs again."),
    JA("実行が書き出したマスクに修正を描き込みます。修正はデータセット横の mask_edits/ に保存され、マスク処理を再実行しても再適用されます。"),
    ZH_HANS("在运行生成的蒙版上绘制修正。修正保存在数据集旁的 mask_edits/ 中，重新运行蒙版处理时会再次应用。"),
    ZH_HANT("在執行產生的遮罩上繪製修正。修正保存在資料集旁的 mask_edits/ 中，重新執行遮罩處理時會再次套用。"),
    KO("실행이 기록한 마스크 위에 수정을 칠합니다. 수정은 데이터셋 옆 mask_edits/에 보관되며 마스킹을 다시 실행해도 다시 적용됩니다."),
    DE("Korrekturen auf die Masken malen, die ein Lauf geschrieben hat. Sie liegen in mask_edits/ neben dem Datensatz und werden nach einem erneuten Maskieren wieder angewendet."),
    FR("Peindre des corrections sur les masques écrits par une exécution. Elles sont conservées dans mask_edits/ à côté du jeu de données et réappliquées quand le masquage est relancé."),
    ES("Pinta correcciones sobre las máscaras que escribió una ejecución. Se guardan en mask_edits/ junto al conjunto de datos y se vuelven a aplicar cuando se repite el enmascarado."),
    PT("Pinte correções sobre as máscaras que uma execução escreveu. Elas ficam em mask_edits/ ao lado do conjunto de dados e são reaplicadas quando o mascaramento é executado de novo."),
    IT("Dipingi correzioni sulle maschere scritte da un'esecuzione. Sono conservate in mask_edits/ accanto al dataset e riapplicate quando il mascheramento viene rieseguito."),
    NL("Schilder correcties op de maskers die een run heeft geschreven. Ze staan in mask_edits/ naast de dataset en worden opnieuw toegepast als het maskeren opnieuw draait."),
    RU("Нанесите исправления на маски, записанные прогоном. Они хранятся в mask_edits/ рядом с набором данных и применяются заново при повторном маскировании."),
    TR("Bir çalıştırmanın yazdığı maskelere düzeltmeler çizin. Düzeltmeler veri kümesinin yanındaki mask_edits/ içinde tutulur ve maskeleme yeniden çalıştığında yeniden uygulanır."));

SS_MSG(window_title,
    EN("Mask correction"),
    JA("マスク修正"),
    ZH_HANS("蒙版修正"),
    ZH_HANT("遮罩修正"),
    KO("마스크 수정"),
    DE("Maskenkorrektur"),
    FR("Correction des masques"),
    ES("Corrección de máscaras"),
    PT("Correção de máscaras"),
    IT("Correzione maschere"),
    NL("Maskercorrectie"),
    RU("Исправление масок"),
    TR("Maske düzeltme"));

SS_MSG(working,
    EN("Loading the frame..."),
    JA("フレームを読み込み中…"),
    ZH_HANS("正在加载帧…"),
    ZH_HANT("正在載入影格…"),
    KO("프레임을 불러오는 중…"),
    DE("Bild wird geladen …"),
    FR("Chargement de l'image…"),
    ES("Cargando el fotograma…"),
    PT("Carregando o quadro…"),
    IT("Caricamento del fotogramma…"),
    NL("Frame wordt geladen…"),
    RU("Загрузка кадра…"),
    TR("Kare yükleniyor…"));

SS_MSG(save,
    EN("Save"),
    JA("保存"),
    ZH_HANS("保存"),
    ZH_HANT("儲存"),
    KO("저장"),
    DE("Speichern"),
    FR("Enregistrer"),
    ES("Guardar"),
    PT("Salvar"),
    IT("Salva"),
    NL("Opslaan"),
    RU("Сохранить"),
    TR("Kaydet"));

SS_MSG(save_help,
    EN("Write the corrected mask into masks/ and the correction layers into mask_edits/. This also happens when you change frame or close the window."),
    JA("修正済みマスクを masks/ に、修正レイヤーを mask_edits/ に書き出します。フレームを切り替えたときやウィンドウを閉じたときにも自動で行われます。"),
    ZH_HANS("将修正后的蒙版写入 masks/，将修正图层写入 mask_edits/。切换帧或关闭窗口时也会自动执行。"),
    ZH_HANT("將修正後的遮罩寫入 masks/，將修正圖層寫入 mask_edits/。切換影格或關閉視窗時也會自動執行。"),
    KO("수정된 마스크를 masks/에, 수정 레이어를 mask_edits/에 기록합니다. 프레임을 바꾸거나 창을 닫을 때도 자동으로 수행됩니다."),
    DE("Die korrigierte Maske nach masks/ und die Korrekturebenen nach mask_edits/ schreiben. Geschieht auch beim Bildwechsel und beim Schließen des Fensters."),
    FR("Écrit le masque corrigé dans masks/ et les calques de correction dans mask_edits/. Se fait aussi au changement d'image et à la fermeture de la fenêtre."),
    ES("Escribe la máscara corregida en masks/ y las capas de corrección en mask_edits/. También ocurre al cambiar de fotograma o cerrar la ventana."),
    PT("Escreve a máscara corrigida em masks/ e as camadas de correção em mask_edits/. Também acontece ao mudar de quadro ou fechar a janela."),
    IT("Scrive la maschera corretta in masks/ e i livelli di correzione in mask_edits/. Avviene anche quando cambi fotogramma o chiudi la finestra."),
    NL("Schrijft het gecorrigeerde masker naar masks/ en de correctielagen naar mask_edits/. Gebeurt ook bij het wisselen van frame en het sluiten van het venster."),
    RU("Записывает исправленную маску в masks/, а слои исправлений в mask_edits/. Происходит также при смене кадра и закрытии окна."),
    TR("Düzeltilmiş maskeyi masks/ içine, düzeltme katmanlarını mask_edits/ içine yazar. Kare değiştirildiğinde ve pencere kapatıldığında da yapılır."));

SS_MSG(revert_frame,
    EN("Revert frame"),
    JA("フレームを元に戻す"),
    ZH_HANS("还原此帧"),
    ZH_HANT("還原此影格"),
    KO("프레임 되돌리기"),
    DE("Bild zurücksetzen"),
    FR("Restaurer l'image"),
    ES("Restablecer fotograma"),
    PT("Repor quadro"),
    IT("Ripristina fotogramma"),
    NL("Frame terugzetten"),
    RU("Вернуть кадр"),
    TR("Kareyi geri al"));

SS_MSG(revert_frame_help,
    EN("Put the mask back exactly as the run wrote it and delete this frame's corrections."),
    JA("マスクを実行が書き出した状態に完全に戻し、このフレームの修正を削除します。"),
    ZH_HANS("将蒙版恢复为运行写出时的原样，并删除此帧的修正。"),
    ZH_HANT("將遮罩恢復為執行寫出時的原樣，並刪除此影格的修正。"),
    KO("마스크를 실행이 기록한 그대로 되돌리고 이 프레임의 수정을 삭제합니다."),
    DE("Die Maske genau so wiederherstellen, wie der Lauf sie geschrieben hat, und die Korrekturen dieses Bildes löschen."),
    FR("Remet le masque exactement tel que l'exécution l'a écrit et supprime les corrections de cette image."),
    ES("Devuelve la máscara exactamente a como la escribió la ejecución y elimina las correcciones de este fotograma."),
    PT("Repõe a máscara exatamente como a execução a escreveu e apaga as correções deste quadro."),
    IT("Riporta la maschera esattamente a come l'esecuzione l'ha scritta ed elimina le correzioni di questo fotogramma."),
    NL("Zet het masker precies terug zoals de run het schreef en verwijdert de correcties van dit frame."),
    RU("Возвращает маску ровно в том виде, в каком её записал прогон, и удаляет исправления этого кадра."),
    TR("Maskeyi çalıştırmanın yazdığı haline tam olarak geri döndürür ve bu karenin düzeltmelerini siler."));

SS_MSG(revert_all,
    EN("Revert all"),
    JA("すべて元に戻す"),
    ZH_HANS("全部还原"),
    ZH_HANT("全部還原"),
    KO("모두 되돌리기"),
    DE("Alle zurücksetzen"),
    FR("Tout restaurer"),
    ES("Restablecer todo"),
    PT("Repor tudo"),
    IT("Ripristina tutto"),
    NL("Alles terugzetten"),
    RU("Вернуть все"),
    TR("Tümünü geri al"));

SS_MSG(revert_all_help,
    EN("Revert every corrected frame of this dataset. No model runs."),
    JA("このデータセットの修正済みフレームをすべて元に戻します。モデルは実行されません。"),
    ZH_HANS("还原此数据集中所有已修正的帧。不会运行模型。"),
    ZH_HANT("還原此資料集中所有已修正的影格。不會執行模型。"),
    KO("이 데이터셋의 수정된 프레임을 모두 되돌립니다. 모델은 실행되지 않습니다."),
    DE("Jedes korrigierte Bild dieses Datensatzes zurücksetzen. Kein Modell läuft."),
    FR("Restaure chaque image corrigée de ce jeu de données. Aucun modèle n'est exécuté."),
    ES("Restablece todos los fotogramas corregidos de este conjunto de datos. No se ejecuta ningún modelo."),
    PT("Repõe todos os quadros corrigidos deste conjunto de dados. Nenhum modelo é executado."),
    IT("Ripristina ogni fotogramma corretto di questo dataset. Nessun modello viene eseguito."),
    NL("Zet elk gecorrigeerd frame van deze dataset terug. Er draait geen model."),
    RU("Возвращает все исправленные кадры этого набора данных. Модель не запускается."),
    TR("Bu veri kümesindeki tüm düzeltilmiş kareleri geri alır. Hiçbir model çalışmaz."));

SS_MSG(undo,
    EN("Undo"),
    JA("元に戻す"),
    ZH_HANS("撤销"),
    ZH_HANT("復原"),
    KO("실행 취소"),
    DE("Rückgängig"),
    FR("Annuler"),
    ES("Deshacer"),
    PT("Desfazer"),
    IT("Annulla"),
    NL("Ongedaan maken"),
    RU("Отменить"),
    TR("Geri al"));

SS_MSG(redo,
    EN("Redo"),
    JA("やり直し"),
    ZH_HANS("重做"),
    ZH_HANT("重做"),
    KO("다시 실행"),
    DE("Wiederholen"),
    FR("Rétablir"),
    ES("Rehacer"),
    PT("Refazer"),
    IT("Ripeti"),
    NL("Opnieuw"),
    RU("Вернуть"),
    TR("Yinele"));

SS_MSG(done,
    EN("Done"),
    JA("完了"),
    ZH_HANS("完成"),
    ZH_HANT("完成"),
    KO("완료"),
    DE("Fertig"),
    FR("Terminé"),
    ES("Listo"),
    PT("Concluído"),
    IT("Fatto"),
    NL("Klaar"),
    RU("Готово"),
    TR("Bitti"));

SS_MSG(tool_eraser,
    EN("Eraser"),     JA("消しゴム"),   ZH_HANS("橡皮擦"), ZH_HANT("橡皮擦"),
    KO("지우개"),      DE("Radierer"),   FR("Gomme"),
    ES("Borrador"),   PT("Borracha"),   IT("Gomma"),
    NL("Gum"),        RU("Ластик"),     TR("Silgi"));

// ===========================================================================
// The status strip
// ===========================================================================

SS_MSG(hint_buttons,
    EN("Drag or Shift+drag: force drop. Ctrl+drag: force keep. Shift+Ctrl+drag: clear the correction. Right click closes a polygon."),
    JA("ドラッグまたは Shift+ドラッグ: 強制的に除外。Ctrl+ドラッグ: 強制的に保持。Shift+Ctrl+ドラッグ: 修正を消去。右クリックで多角形を閉じる。"),
    ZH_HANS("拖动或 Shift+拖动：强制丢弃。Ctrl+拖动：强制保留。Shift+Ctrl+拖动：清除修正。右键单击闭合多边形。"),
    ZH_HANT("拖曳或 Shift+拖曳：強制捨棄。Ctrl+拖曳：強制保留。Shift+Ctrl+拖曳：清除修正。右鍵點一下閉合多邊形。"),
    KO("드래그 또는 Shift+드래그: 강제 제외. Ctrl+드래그: 강제 유지. Shift+Ctrl+드래그: 수정 지우기. 오른쪽 클릭으로 다각형 닫기."),
    DE("Ziehen oder Shift+Ziehen: erzwungen verwerfen. Ctrl+Ziehen: erzwungen behalten. Shift+Ctrl+Ziehen: Korrektur löschen. Rechtsklick schließt ein Polygon."),
    FR("Glisser ou Shift+glisser : exclusion forcée. Ctrl+glisser : conservation forcée. Shift+Ctrl+glisser : effacer la correction. Un clic droit ferme un polygone."),
    ES("Arrastrar o Shift+arrastrar: descartar a la fuerza. Ctrl+arrastrar: conservar a la fuerza. Shift+Ctrl+arrastrar: borrar la corrección. Un clic derecho cierra un polígono."),
    PT("Arrastar ou Shift+arrastar: descartar à força. Ctrl+arrastar: manter à força. Shift+Ctrl+arrastar: apagar a correção. Um clique direito fecha um polígono."),
    IT("Trascina o Shift+trascina: scarta forzatamente. Ctrl+trascina: mantieni forzatamente. Shift+Ctrl+trascina: cancella la correzione. Un clic destro chiude un poligono."),
    NL("Slepen of Shift+slepen: geforceerd weglaten. Ctrl+slepen: geforceerd behouden. Shift+Ctrl+slepen: correctie wissen. Rechtsklik sluit een veelhoek."),
    RU("Перетаскивание или Shift+перетаскивание: принудительно убрать. Ctrl+перетаскивание: принудительно оставить. Shift+Ctrl+перетаскивание: стереть исправление. Правый щелчок замыкает многоугольник."),
    TR("Sürükleme veya Shift+sürükleme: zorla at. Ctrl+sürükleme: zorla tut. Shift+Ctrl+sürükleme: düzeltmeyi sil. Sağ tık çokgeni kapatır."));

SS_MSG(hint_view,
    EN("Wheel: zoom. Alt+wheel, [ and ]: brush or eraser size. Middle drag or Space+drag: pan. Esc: cancel the stroke."),
    JA("ホイール: ズーム。Alt+ホイール、[ と ]: ブラシまたは消しゴムのサイズ。中ボタンドラッグまたは Space+ドラッグ: 移動。Esc: ストロークを取り消し。"),
    ZH_HANS("滚轮：缩放。Alt+滚轮、[ 和 ]：画笔或橡皮擦大小。中键拖动或 Space+拖动：平移。Esc：取消笔画。"),
    ZH_HANT("滾輪：縮放。Alt+滾輪、[ 和 ]：筆刷或橡皮擦大小。中鍵拖曳或 Space+拖曳：平移。Esc：取消筆畫。"),
    KO("휠: 확대/축소. Alt+휠, [ 와 ]: 브러시 또는 지우개 크기. 가운데 버튼 드래그 또는 Space+드래그: 이동. Esc: 획 취소."),
    DE("Rad: Zoom. Alt+Rad, [ und ]: Pinsel- oder Radierergröße. Mittlere Taste ziehen oder Leertaste+Ziehen: verschieben. Esc: Strich abbrechen."),
    FR("Molette : zoom. Alt+molette, [ et ] : taille du pinceau ou de la gomme. Glisser avec le bouton du milieu ou Espace+glisser : déplacer. Échap : annuler le tracé."),
    ES("Rueda: zoom. Alt+rueda, [ y ]: tamaño del pincel o del borrador. Arrastrar con el botón central o Espacio+arrastrar: desplazar. Esc: cancelar el trazo."),
    PT("Roda: zoom. Alt+roda, [ e ]: tamanho do pincel ou da borracha. Arrastar com o botão do meio ou Espaço+arrastar: deslocar. Esc: cancelar o traço."),
    IT("Rotella: zoom. Alt+rotella, [ e ]: dimensione del pennello o della gomma. Trascina con il tasto centrale o Spazio+trascina: sposta. Esc: annulla il tratto."),
    NL("Wiel: zoomen. Alt+wiel, [ en ]: penseel- of gumgrootte. Slepen met middelste knop of spatie+slepen: verschuiven. Esc: streek annuleren."),
    RU("Колесо: масштаб. Alt+колесо, [ и ]: размер кисти или ластика. Перетаскивание средней кнопкой или Пробел+перетаскивание: сдвиг. Esc: отменить штрих."),
    TR("Tekerlek: yakınlaştırma. Alt+tekerlek, [ ve ]: fırça veya silgi boyutu. Orta tuşla veya Boşluk+sürükleme: kaydırma. Esc: çizimi iptal et."));

SS_MSG(hint_eraser,
    EN("Drag or Shift+drag: force keep. Ctrl+drag: force drop. Shift+Ctrl+drag: clear the correction."),
    JA("ドラッグまたは Shift+ドラッグ: 強制的に保持。Ctrl+ドラッグ: 強制的に除外。Shift+Ctrl+ドラッグ: 修正を消去。"),
    ZH_HANS("拖动或 Shift+拖动：强制保留。Ctrl+拖动：强制丢弃。Shift+Ctrl+拖动：清除修正。"),
    ZH_HANT("拖曳或 Shift+拖曳：強制保留。Ctrl+拖曳：強制捨棄。Shift+Ctrl+拖曳：清除修正。"),
    KO("드래그 또는 Shift+드래그: 강제 유지. Ctrl+드래그: 강제 제외. Shift+Ctrl+드래그: 수정 지우기."),
    DE("Ziehen oder Shift+Ziehen: erzwungen behalten. Ctrl+Ziehen: erzwungen verwerfen. Shift+Ctrl+Ziehen: Korrektur löschen."),
    FR("Glisser ou Shift+glisser : conservation forcée. Ctrl+glisser : exclusion forcée. Shift+Ctrl+glisser : effacer la correction."),
    ES("Arrastrar o Shift+arrastrar: conservar a la fuerza. Ctrl+arrastrar: descartar a la fuerza. Shift+Ctrl+arrastrar: borrar la corrección."),
    PT("Arrastar ou Shift+arrastar: manter à força. Ctrl+arrastar: descartar à força. Shift+Ctrl+arrastar: apagar a correção."),
    IT("Trascina o Shift+trascina: mantieni forzatamente. Ctrl+trascina: scarta forzatamente. Shift+Ctrl+trascina: cancella la correzione."),
    NL("Slepen of Shift+slepen: geforceerd behouden. Ctrl+slepen: geforceerd weglaten. Shift+Ctrl+slepen: correctie wissen."),
    RU("Перетаскивание или Shift+перетаскивание: принудительно оставить. Ctrl+перетаскивание: принудительно убрать. Shift+Ctrl+перетаскивание: стереть исправление."),
    TR("Sürükleme veya Shift+sürükleme: zorla tut. Ctrl+sürükleme: zorla at. Shift+Ctrl+sürükleme: düzeltmeyi sil."));

SS_MSG(status_frame,
    EN("Frame {0} of {1}: {2}"),
    JA("フレーム {0} / {1}: {2}"),
    ZH_HANS("第 {0} 帧，共 {1} 帧：{2}"),
    ZH_HANT("第 {0} 格，共 {1} 格：{2}"),
    KO("프레임 {0} / {1}: {2}"),
    DE("Bild {0} von {1}: {2}"),
    FR("Image {0} sur {1} : {2}"),
    ES("Fotograma {0} de {1}: {2}"),
    PT("Quadro {0} de {1}: {2}"),
    IT("Fotogramma {0} di {1}: {2}"),
    NL("Frame {0} van {1}: {2}"),
    RU("Кадр {0} из {1}: {2}"),
    TR("Kare {0} / {1}: {2}"));

SS_MSG(status_camera,
    EN("Camera: {0}"),
    JA("カメラ: {0}"),
    ZH_HANS("相机：{0}"),
    ZH_HANT("相機：{0}"),
    KO("카메라: {0}"),
    DE("Kamera: {0}"),
    FR("Caméra : {0}"),
    ES("Cámara: {0}"),
    PT("Câmera: {0}"),
    IT("Fotocamera: {0}"),
    NL("Camera: {0}"),
    RU("Камера: {0}"),
    TR("Kamera: {0}"));

SS_MSG(status_kept,
    EN("Kept: {0}%"),
    JA("保持: {0}%"),
    ZH_HANS("保留：{0}%"),
    ZH_HANT("保留：{0}%"),
    KO("유지: {0}%"),
    DE("Behalten: {0}%"),
    FR("Conservé : {0}%"),
    ES("Conservado: {0}%"),
    PT("Mantido: {0}%"),
    IT("Mantenuto: {0}%"),
    NL("Behouden: {0}%"),
    RU("Оставлено: {0}%"),
    TR("Tutulan: %{0}"));

SS_MSG(status_unsaved,
    EN("Unsaved changes"),
    JA("未保存の変更"),
    ZH_HANS("有未保存的更改"),
    ZH_HANT("有未儲存的變更"),
    KO("저장되지 않은 변경 사항"),
    DE("Ungespeicherte Änderungen"),
    FR("Modifications non enregistrées"),
    ES("Cambios sin guardar"),
    PT("Alterações não salvas"),
    IT("Modifiche non salvate"),
    NL("Niet-opgeslagen wijzigingen"),
    RU("Несохранённые изменения"),
    TR("Kaydedilmemiş değişiklikler"));

SS_MSG(status_saved,
    EN("Saved"),
    JA("保存済み"),
    ZH_HANS("已保存"),
    ZH_HANT("已儲存"),
    KO("저장됨"),
    DE("Gespeichert"),
    FR("Enregistré"),
    ES("Guardado"),
    PT("Salvo"),
    IT("Salvato"),
    NL("Opgeslagen"),
    RU("Сохранено"),
    TR("Kaydedildi"));

SS_MSG(status_base_regenerated,
    EN("A run regenerated this mask; the corrections were re-applied over the new one."),
    JA("実行によりこのマスクが再生成されたため、新しいマスクの上に修正を再適用しました。"),
    ZH_HANS("运行重新生成了此蒙版；修正已重新应用到新蒙版上。"),
    ZH_HANT("執行重新產生了此遮罩；修正已重新套用到新遮罩上。"),
    KO("실행이 이 마스크를 다시 생성했습니다. 수정은 새 마스크 위에 다시 적용되었습니다."),
    DE("Ein Lauf hat diese Maske neu erzeugt; die Korrekturen wurden auf die neue angewendet."),
    FR("Une exécution a régénéré ce masque ; les corrections ont été réappliquées sur le nouveau."),
    ES("Una ejecución regeneró esta máscara; las correcciones se volvieron a aplicar sobre la nueva."),
    PT("Uma execução regenerou esta máscara; as correções foram reaplicadas sobre a nova."),
    IT("Un'esecuzione ha rigenerato questa maschera; le correzioni sono state riapplicate sulla nuova."),
    NL("Een run heeft dit masker opnieuw gemaakt; de correcties zijn opnieuw toegepast op het nieuwe."),
    RU("Прогон заново создал эту маску; исправления применены поверх новой."),
    TR("Bir çalıştırma bu maskeyi yeniden oluşturdu; düzeltmeler yenisinin üzerine yeniden uygulandı."));

SS_MSG(status_base_missing,
    EN("No mask on disk for this frame. The corrections are kept until a run writes one."),
    JA("このフレームのマスクがディスクにありません。実行がマスクを書き出すまで修正は保持されます。"),
    ZH_HANS("磁盘上没有此帧的蒙版。修正将保留到某次运行写出蒙版为止。"),
    ZH_HANT("磁碟上沒有此影格的遮罩。修正會保留到某次執行寫出遮罩為止。"),
    KO("이 프레임의 마스크가 디스크에 없습니다. 실행이 마스크를 기록할 때까지 수정은 보관됩니다."),
    DE("Für dieses Bild liegt keine Maske auf der Platte. Die Korrekturen bleiben erhalten, bis ein Lauf eine schreibt."),
    FR("Aucun masque sur le disque pour cette image. Les corrections sont conservées jusqu'à ce qu'une exécution en écrive un."),
    ES("No hay máscara en disco para este fotograma. Las correcciones se conservan hasta que una ejecución escriba una."),
    PT("Não há máscara em disco para este quadro. As correções ficam guardadas até uma execução escrever uma."),
    IT("Nessuna maschera su disco per questo fotogramma. Le correzioni restano finché un'esecuzione non ne scrive una."),
    NL("Geen masker op schijf voor dit frame. De correcties blijven bewaard tot een run er een schrijft."),
    RU("На диске нет маски для этого кадра. Исправления сохраняются, пока прогон не запишет её."),
    TR("Bu kare için diskte maske yok. Bir çalıştırma maske yazana kadar düzeltmeler saklanır."));

SS_MSG(status_commit,
    EN("Last stroke: {0} ms"),
    JA("直前のストローク: {0} ms"),
    ZH_HANS("上一笔：{0} ms"),
    ZH_HANT("上一筆：{0} ms"),
    KO("마지막 획: {0} ms"),
    DE("Letzter Strich: {0} ms"),
    FR("Dernier tracé : {0} ms"),
    ES("Último trazo: {0} ms"),
    PT("Último traço: {0} ms"),
    IT("Ultimo tratto: {0} ms"),
    NL("Laatste streek: {0} ms"),
    RU("Последний штрих: {0} мс"),
    TR("Son çizim: {0} ms"));

SS_MSG(brush_radius,
    EN("Brush: {0} px"),
    JA("ブラシ: {0} px"),
    ZH_HANS("画笔：{0} px"),
    ZH_HANT("筆刷：{0} px"),
    KO("브러시: {0} px"),
    DE("Pinsel: {0} px"),
    FR("Pinceau : {0} px"),
    ES("Pincel: {0} px"),
    PT("Pincel: {0} px"),
    IT("Pennello: {0} px"),
    NL("Penseel: {0} px"),
    RU("Кисть: {0} px"),
    TR("Fırça: {0} px"));

SS_MSG(eraser_radius,
    EN("Eraser: {0} px"),
    JA("消しゴム: {0} px"),
    ZH_HANS("橡皮擦：{0} px"),
    ZH_HANT("橡皮擦：{0} px"),
    KO("지우개: {0} px"),
    DE("Radierer: {0} px"),
    FR("Gomme : {0} px"),
    ES("Borrador: {0} px"),
    PT("Borracha: {0} px"),
    IT("Gomma: {0} px"),
    NL("Gum: {0} px"),
    RU("Ластик: {0} px"),
    TR("Silgi: {0} px"));

SS_MSG(radius_keys,
    EN("[ ] Alt+wheel"),
    JA("[ ] Alt+ホイール"),
    ZH_HANS("[ ] Alt+滚轮"),
    ZH_HANT("[ ] Alt+滾輪"),
    KO("[ ] Alt+휠"),
    DE("[ ] Alt+Rad"),
    FR("[ ] Alt+molette"),
    ES("[ ] Alt+rueda"),
    PT("[ ] Alt+roda"),
    IT("[ ] Alt+rotella"),
    NL("[ ] Alt+wiel"),
    RU("[ ] Alt+колесо"),
    TR("[ ] Alt+tekerlek"));

SS_MSG(radius_help,
    EN("Brush and eraser size, in mask pixels. [ and ] step it; Alt+wheel over the canvas is continuous."),
    JA("ブラシと消しゴムのサイズ（マスクのピクセル単位）。[ と ] は段階的に、キャンバス上の Alt+ホイールは連続的に変えます。"),
    ZH_HANS("画笔和橡皮擦的大小，以蒙版像素计。[ 和 ] 逐级调整；在画布上 Alt+滚轮连续调整。"),
    ZH_HANT("筆刷和橡皮擦的大小，以遮罩像素計。[ 和 ] 逐級調整；在畫布上 Alt+滾輪連續調整。"),
    KO("브러시와 지우개의 크기(마스크 픽셀). [ 와 ] 는 단계적으로, 캔버스 위의 Alt+휠은 연속적으로 바꿉니다."),
    DE("Größe von Pinsel und Radierer, in Maskenpixeln. [ und ] ändern sie schrittweise, Alt+Rad über der Leinwand stufenlos."),
    FR("Taille du pinceau et de la gomme, en pixels de masque. [ et ] la changent par paliers ; Alt+molette sur le canevas, en continu."),
    ES("Tamaño del pincel y del borrador, en píxeles de máscara. [ y ] lo cambian por pasos; Alt+rueda sobre el lienzo, de forma continua."),
    PT("Tamanho do pincel e da borracha, em pixels da máscara. [ e ] alteram-no por passos; Alt+roda sobre a tela, de forma contínua."),
    IT("Dimensione del pennello e della gomma, in pixel della maschera. [ e ] la cambiano a passi; Alt+rotella sulla tela, in modo continuo."),
    NL("Grootte van penseel en gum, in maskerpixels. [ en ] wijzigen die stapsgewijs, Alt+wiel boven het canvas vloeiend."),
    RU("Размер кисти и ластика, в пикселях маски. [ и ] меняют его ступенчато, Alt+колесо над холстом — плавно."),
    TR("Fırça ve silgi boyutu, maske pikseli cinsinden. [ ve ] adım adım değiştirir; tuval üzerinde Alt+tekerlek sürekli değiştirir."));

SS_MSG(corrected_count,
    EN("Corrected frames: {0}"),
    JA("修正済みフレーム: {0}"),
    ZH_HANS("已修正的帧：{0}"),
    ZH_HANT("已修正的影格：{0}"),
    KO("수정된 프레임: {0}"),
    DE("Korrigierte Bilder: {0}"),
    FR("Images corrigées : {0}"),
    ES("Fotogramas corregidos: {0}"),
    PT("Quadros corrigidos: {0}"),
    IT("Fotogrammi corretti: {0}"),
    NL("Gecorrigeerde frames: {0}"),
    RU("Исправленных кадров: {0}"),
    TR("Düzeltilen kareler: {0}"));

// ===========================================================================
// Errors, and the two lines the dataset run logs
// ===========================================================================

SS_MSG(err_workspace_inside_images,
    EN("The dataset folder is inside the photo folder, so the corrections have nowhere safe to live. Choose another output folder."),
    JA("データセットフォルダーが写真フォルダーの中にあるため、修正を安全に保存できる場所がありません。別の出力フォルダーを選んでください。"),
    ZH_HANS("数据集文件夹位于照片文件夹内，修正没有安全的存放位置。请选择其他输出文件夹。"),
    ZH_HANT("資料集資料夾位於照片資料夾內，修正沒有安全的存放位置。請選擇其他輸出資料夾。"),
    KO("데이터셋 폴더가 사진 폴더 안에 있어 수정을 안전하게 둘 곳이 없습니다. 다른 출력 폴더를 선택하세요."),
    DE("Der Datensatzordner liegt im Fotoordner, die Korrekturen hätten also keinen sicheren Platz. Wählen Sie einen anderen Ausgabeordner."),
    FR("Le dossier du jeu de données est dans le dossier des photos, les corrections n'ont donc aucun endroit sûr. Choisissez un autre dossier de sortie."),
    ES("La carpeta del conjunto de datos está dentro de la carpeta de fotos, así que las correcciones no tienen un lugar seguro. Elige otra carpeta de salida."),
    PT("A pasta do conjunto de dados está dentro da pasta das fotos, por isso as correções não têm um lugar seguro. Escolha outra pasta de saída."),
    IT("La cartella del dataset è dentro la cartella delle foto, quindi le correzioni non hanno un posto sicuro. Scegli un'altra cartella di output."),
    NL("De datasetmap staat in de fotomap, dus de correcties hebben geen veilige plek. Kies een andere uitvoermap."),
    RU("Папка набора данных находится внутри папки с фотографиями, поэтому исправлениям негде безопасно храниться. Выберите другую папку вывода."),
    TR("Veri kümesi klasörü fotoğraf klasörünün içinde olduğundan düzeltmelerin güvenle duracağı bir yer yok. Başka bir çıktı klasörü seçin."));

SS_MSG(err_no_frames,
    EN("No images were found under {0}."),
    JA("{0} の下に画像が見つかりませんでした。"),
    ZH_HANS("在 {0} 下未找到图像。"),
    ZH_HANT("在 {0} 下找不到影像。"),
    KO("{0} 아래에서 이미지를 찾지 못했습니다."),
    DE("Unter {0} wurden keine Bilder gefunden."),
    FR("Aucune image trouvée sous {0}."),
    ES("No se encontraron imágenes en {0}."),
    PT("Não foram encontradas imagens em {0}."),
    IT("Nessuna immagine trovata in {0}."),
    NL("Geen afbeeldingen gevonden onder {0}."),
    RU("В {0} не найдено изображений."),
    TR("{0} altında görüntü bulunamadı."));

SS_MSG(err_read,
    EN("Could not read {0}."),
    JA("{0} を読み込めませんでした。"),
    ZH_HANS("无法读取 {0}。"),
    ZH_HANT("無法讀取 {0}。"),
    KO("{0}을(를) 읽을 수 없습니다."),
    DE("{0} konnte nicht gelesen werden."),
    FR("Impossible de lire {0}."),
    ES("No se pudo leer {0}."),
    PT("Não foi possível ler {0}."),
    IT("Impossibile leggere {0}."),
    NL("Kon {0} niet lezen."),
    RU("Не удалось прочитать {0}."),
    TR("{0} okunamadı."));

SS_MSG(err_write,
    EN("Could not write {0}."),
    JA("{0} を書き込めませんでした。"),
    ZH_HANS("无法写入 {0}。"),
    ZH_HANT("無法寫入 {0}。"),
    KO("{0}을(를) 쓸 수 없습니다."),
    DE("{0} konnte nicht geschrieben werden."),
    FR("Impossible d'écrire {0}."),
    ES("No se pudo escribir {0}."),
    PT("Não foi possível escrever {0}."),
    IT("Impossibile scrivere {0}."),
    NL("Kon {0} niet schrijven."),
    RU("Не удалось записать {0}."),
    TR("{0} yazılamadı."));

SS_MSG(err_other_mask_root,
    EN("The corrections in this project were made against the mask folder {0}, so they cannot be read against another one. Point the run back at that folder, or discard the corrections."),
    JA("このプロジェクトの修正はマスクフォルダー {0} に対して行われたため、別のフォルダーには適用できません。実行をそのフォルダーに戻すか、修正を破棄してください。"),
    ZH_HANS("本项目的修正是针对蒙版文件夹 {0} 做的，无法用于其他文件夹。请把运行指回该文件夹，或放弃这些修正。"),
    ZH_HANT("本專案的修正是針對遮罩資料夾 {0} 做的，無法用於其他資料夾。請把執行指回該資料夾，或捨棄這些修正。"),
    KO("이 프로젝트의 수정은 마스크 폴더 {0}을(를) 기준으로 이루어졌으므로 다른 폴더에는 적용할 수 없습니다. 실행을 그 폴더로 되돌리거나 수정을 버리세요."),
    DE("Die Korrekturen dieses Projekts entstanden gegen den Maskenordner {0} und lassen sich nicht auf einen anderen anwenden. Richten Sie den Lauf wieder auf diesen Ordner oder verwerfen Sie die Korrekturen."),
    FR("Les corrections de ce projet ont été faites sur le dossier de masques {0} et ne peuvent pas servir pour un autre. Repointez le traitement sur ce dossier, ou abandonnez les corrections."),
    ES("Las correcciones de este proyecto se hicieron sobre la carpeta de máscaras {0}, así que no sirven para otra. Vuelve a apuntar la ejecución a esa carpeta o descarta las correcciones."),
    PT("As correções deste projeto foram feitas sobre a pasta de máscaras {0}, por isso não servem para outra. Aponte a execução de volta para essa pasta ou descarte as correções."),
    IT("Le correzioni di questo progetto sono state fatte sulla cartella di maschere {0}, quindi non valgono per un'altra. Riporta l'esecuzione su quella cartella, oppure scarta le correzioni."),
    NL("De correcties in dit project zijn gemaakt op de maskermap {0} en gelden niet voor een andere. Richt de run weer op die map, of gooi de correcties weg."),
    RU("Исправления в этом проекте сделаны для папки масок {0}, поэтому к другой они неприменимы. Верните запуск к этой папке или откажитесь от исправлений."),
    TR("Bu projedeki düzeltmeler {0} maske klasörüne göre yapıldı, bu yüzden başka bir klasöre uygulanamaz. Çalışmayı o klasöre geri yönlendirin ya da düzeltmeleri atın."));

SS_MSG(err_other_mask_polarity,
    EN("The corrections in this project were made while the masks were read the other way round. Put \"Flip masks\" back as it was, or discard the corrections."),
    JA("このプロジェクトの修正は、マスクを逆の意味で読んでいたときに行われました。「マスクを反転」を元に戻すか、修正を破棄してください。"),
    ZH_HANS("本项目的修正是在以相反方式读取蒙版时做的。请把“反转蒙版”改回原样，或放弃这些修正。"),
    ZH_HANT("本專案的修正是在以相反方式讀取遮罩時做的。請把「反轉遮罩」改回原樣，或捨棄這些修正。"),
    KO("이 프로젝트의 수정은 마스크를 반대로 읽던 때에 이루어졌습니다. \"마스크 반전\"을 원래대로 되돌리거나 수정을 버리세요."),
    DE("Die Korrekturen dieses Projekts entstanden, als die Masken umgekehrt gelesen wurden. Stellen Sie \"Masken umkehren\" zurück oder verwerfen Sie die Korrekturen."),
    FR("Les corrections de ce projet ont été faites quand les masques étaient lus dans l'autre sens. Remettez « Inverser les masques » comme avant, ou abandonnez les corrections."),
    ES("Las correcciones de este proyecto se hicieron cuando las máscaras se leían al revés. Deja «Invertir las máscaras» como estaba o descarta las correcciones."),
    PT("As correções deste projeto foram feitas quando as máscaras eram lidas ao contrário. Reponha \"Inverter as máscaras\" como estava ou descarte as correções."),
    IT("Le correzioni di questo progetto sono state fatte quando le maschere erano lette al contrario. Rimetti \"Invertire le maschere\" com'era, oppure scarta le correzioni."),
    NL("De correcties in dit project zijn gemaakt toen de maskers andersom werden gelezen. Zet \"Maskers omkeren\" terug zoals het was, of gooi de correcties weg."),
    RU("Исправления в этом проекте сделаны, когда маски читались наоборот. Верните «Инвертировать маски» как было или откажитесь от исправлений."),
    TR("Bu projedeki düzeltmeler maskeler ters okunurken yapıldı. \"Maskeleri ters çevir\" ayarını eski hâline getirin ya da düzeltmeleri atın."));

SS_MSG(err_size_mismatch,
    EN("The correction layer for {0} is {1}x{2} but the mask is {3}x{4}; the layer was ignored."),
    JA("{0} の修正レイヤーは {1}x{2} ですが、マスクは {3}x{4} です。レイヤーは無視されました。"),
    ZH_HANS("{0} 的修正图层为 {1}x{2}，但蒙版为 {3}x{4}；该图层已被忽略。"),
    ZH_HANT("{0} 的修正圖層為 {1}x{2}，但遮罩為 {3}x{4}；該圖層已被忽略。"),
    KO("{0}의 수정 레이어는 {1}x{2}이지만 마스크는 {3}x{4}입니다. 레이어를 무시했습니다."),
    DE("Die Korrekturebene für {0} ist {1}x{2}, die Maske aber {3}x{4}; die Ebene wurde ignoriert."),
    FR("Le calque de correction de {0} fait {1}x{2} mais le masque fait {3}x{4} ; le calque a été ignoré."),
    ES("La capa de corrección de {0} es de {1}x{2} pero la máscara es de {3}x{4}; la capa se ignoró."),
    PT("A camada de correção de {0} tem {1}x{2} mas a máscara tem {3}x{4}; a camada foi ignorada."),
    IT("Il livello di correzione di {0} è {1}x{2} ma la maschera è {3}x{4}; il livello è stato ignorato."),
    NL("De correctielaag van {0} is {1}x{2} maar het masker is {3}x{4}; de laag is genegeerd."),
    RU("Слой исправлений для {0} имеет размер {1}x{2}, а маска {3}x{4}; слой пропущен."),
    TR("{0} için düzeltme katmanı {1}x{2} ama maske {3}x{4}; katman yok sayıldı."));

SS_MSG(log_recomposited,
    EN("Mask corrections re-applied over regenerated masks: {0}"),
    JA("再生成されたマスクへの修正の再適用: {0}"),
    ZH_HANS("已重新应用到重新生成蒙版上的修正：{0}"),
    ZH_HANT("已重新套用到重新產生遮罩上的修正：{0}"),
    KO("다시 생성된 마스크 위에 다시 적용한 수정: {0}"),
    DE("Auf neu erzeugte Masken wieder angewendete Korrekturen: {0}"),
    FR("Corrections réappliquées sur des masques régénérés : {0}"),
    ES("Correcciones reaplicadas sobre máscaras regeneradas: {0}"),
    PT("Correções reaplicadas sobre máscaras regeneradas: {0}"),
    IT("Correzioni riapplicate su maschere rigenerate: {0}"),
    NL("Correcties opnieuw toegepast op opnieuw gemaakte maskers: {0}"),
    RU("Исправлений применено поверх заново созданных масок: {0}"),
    TR("Yeniden oluşturulan maskelere yeniden uygulanan düzeltmeler: {0}"));

SS_MSG(log_recomposite_failed,
    EN("Could not re-apply the mask corrections: {0}"),
    JA("マスクの修正を再適用できませんでした: {0}"),
    ZH_HANS("无法重新应用蒙版修正：{0}"),
    ZH_HANT("無法重新套用遮罩修正：{0}"),
    KO("마스크 수정을 다시 적용할 수 없습니다: {0}"),
    DE("Die Maskenkorrekturen konnten nicht wieder angewendet werden: {0}"),
    FR("Impossible de réappliquer les corrections de masque : {0}"),
    ES("No se pudieron reaplicar las correcciones de máscara: {0}"),
    PT("Não foi possível reaplicar as correções de máscara: {0}"),
    IT("Impossibile riapplicare le correzioni delle maschere: {0}"),
    NL("Kon de maskercorrecties niet opnieuw toepassen: {0}"),
    RU("Не удалось заново применить исправления масок: {0}"),
    TR("Maske düzeltmeleri yeniden uygulanamadı: {0}"));

// ===========================================================================
// The pen tool (plan 2)
// ===========================================================================

SS_MSG(tool_path,
    EN("Path"),       JA("パス"),       ZH_HANS("路径"),  ZH_HANT("路徑"),
    KO("패스"),        DE("Pfad"),       FR("Chemin"),
    ES("Trazado"),    PT("Traçado"),    IT("Tracciato"),
    NL("Pad"),        RU("Контур"),     TR("Yol"));

SS_MSG(hint_path,
    EN("Click along an edge to drop anchors; the path snaps to the edge. Ctrl+click the "
       "first anchor to keep instead, Shift+Ctrl to clear. Click the first anchor, Enter "
       "or right-click closes it and paints its inside. Ctrl+Z takes an anchor back; Esc "
       "cancels."),
    JA("輪郭に沿ってクリックして点を置くと、パスが輪郭に沿います。最初の点を Ctrl+クリック"
       "すると保持になり、Shift+Ctrl でクリアします。最初の点をクリックするか Enter か右ク"
       "リックで閉じ、内側を塗ります。Ctrl+Z で点を戻し、Esc で中止します。"),
    ZH_HANS("沿边缘点击放下锚点，路径会贴合边缘。Ctrl+点击第一个锚点改为保留，Shift+Ctrl "
            "则清除。点击第一个锚点、按 Enter 或右键闭合并涂抹其内部。Ctrl+Z 撤回一个锚点，"
            "Esc 取消。"),
    ZH_HANT("沿邊緣點擊放下錨點，路徑會貼合邊緣。Ctrl+點擊第一個錨點改為保留，Shift+Ctrl "
            "則清除。點擊第一個錨點、按 Enter 或右鍵閉合並塗抹其內部。Ctrl+Z 收回一個錨點，"
            "Esc 取消。"),
    KO("윤곽을 따라 클릭해 앵커를 놓으면 경로가 윤곽에 붙습니다. 첫 앵커를 Ctrl+클릭하면 "
       "유지로 바뀌고, Shift+Ctrl은 지웁니다. 첫 앵커 클릭, Enter 또는 오른쪽 클릭으로 닫고 "
       "안쪽을 칠합니다. Ctrl+Z는 앵커를 되돌리고 Esc는 취소합니다."),
    DE("Entlang einer Kante klicken, um Anker zu setzen; der Pfad legt sich an die Kante. "
       "Strg+Klick auf den ersten Anker behält ihn stattdessen, Umschalt+Strg löscht die "
       "Korrektur. Erster Anker, Eingabe oder Rechtsklick schließt ihn und malt sein "
       "Inneres. Strg+Z nimmt einen Anker zurück, Esc bricht ab."),
    FR("Cliquez le long d'un contour pour poser des ancres ; le chemin épouse le contour. "
       "Ctrl+clic sur la première ancre la conserve à la place, Shift+Ctrl efface. La "
       "première ancre, Entrée ou un clic droit le ferme et peint son intérieur. Ctrl+Z "
       "retire une ancre, Échap annule."),
    ES("Haga clic a lo largo de un borde para poner anclas; el trazado se ajusta al borde. "
       "Ctrl+clic en la primera ancla la conserva en su lugar, Shift+Ctrl la borra. La "
       "primera ancla, Intro o clic derecho lo cierra y pinta su interior. Ctrl+Z quita "
       "un ancla; Esc cancela."),
    PT("Clique ao longo de um contorno para pôr âncoras; o traçado cola-se ao contorno. "
       "Ctrl+clique na primeira âncora mantém-na em vez disso, Shift+Ctrl apaga. A "
       "primeira âncora, Enter ou clique direito fecha-o e pinta o interior. Ctrl+Z retira "
       "uma âncora; Esc cancela."),
    IT("Fai clic lungo un bordo per posare ancoraggi; il tracciato segue il bordo. Ctrl+clic "
       "sul primo ancoraggio lo mantiene invece, Shift+Ctrl cancella. Il primo ancoraggio, "
       "Invio o clic destro lo chiude e ne dipinge l'interno. Ctrl+Z toglie un ancoraggio; "
       "Esc annulla."),
    NL("Klik langs een rand om ankers te zetten; het pad volgt de rand. Ctrl+klik op het "
       "eerste anker behoudt het juist, Shift+Ctrl wist. Het eerste anker, Enter of "
       "rechtsklik sluit het en schildert de binnenkant. Ctrl+Z neemt een anker terug; Esc "
       "breekt af."),
    RU("Щёлкайте вдоль края, чтобы ставить опорные точки; контур прилипает к краю. "
       "Ctrl+щелчок по первой точке сохраняет её, Shift+Ctrl стирает. Первая точка, Enter "
       "или правая кнопка замыкают его и закрашивают внутренность. Ctrl+Z убирает точку, "
       "Esc отменяет."),
    TR("Kenar boyunca tıklayarak çapalar bırakın; yol kenara yapışır. İlk çapaya "
       "Ctrl+tıklamak onu tutar, Shift+Ctrl siler. İlk çapa, Enter veya sağ tık onu kapatır "
       "ve içini boyar. Ctrl+Z bir çapayı geri alır; Esc iptal eder."));

SS_MSG(path_building,
    EN("Preparing the edge map for this frame..."),
    JA("このフレームの輪郭マップを準備中..."),
    ZH_HANS("正在为此帧准备边缘图..."),
    ZH_HANT("正在為此影格準備邊緣圖..."),
    KO("이 프레임의 윤곽 맵을 준비하는 중..."),
    DE("Kantenkarte für dieses Bild wird vorbereitet..."),
    FR("Préparation de la carte des contours de cette image..."),
    ES("Preparando el mapa de bordes de este fotograma..."),
    PT("A preparar o mapa de contornos deste quadro..."),
    IT("Preparazione della mappa dei bordi di questo fotogramma..."),
    NL("Randkaart voor dit frame wordt voorbereid..."),
    RU("Подготовка карты краёв для этого кадра..."),
    TR("Bu kare için kenar haritası hazırlanıyor..."));

SS_MSG(path_edge_map,
    EN("Edge map: {0}x{1}, step {2}, built in {3} ms"),
    JA("輪郭マップ: {0}x{1}、間隔 {2}、作成 {3} ms"),
    ZH_HANS("边缘图：{0}x{1}，步长 {2}，用时 {3} ms"),
    ZH_HANT("邊緣圖：{0}x{1}，步長 {2}，用時 {3} ms"),
    KO("윤곽 맵: {0}x{1}, 간격 {2}, 생성 {3} ms"),
    DE("Kantenkarte: {0}x{1}, Schritt {2}, erstellt in {3} ms"),
    FR("Carte des contours : {0}x{1}, pas {2}, calculée en {3} ms"),
    ES("Mapa de bordes: {0}x{1}, paso {2}, calculado en {3} ms"),
    PT("Mapa de contornos: {0}x{1}, passo {2}, calculado em {3} ms"),
    IT("Mappa dei bordi: {0}x{1}, passo {2}, calcolata in {3} ms"),
    NL("Randkaart: {0}x{1}, stap {2}, gemaakt in {3} ms"),
    RU("Карта краёв: {0}x{1}, шаг {2}, построена за {3} мс"),
    TR("Kenar haritası: {0}x{1}, adım {2}, {3} ms içinde oluşturuldu"));

SS_MSG(path_straight,
    EN("No edge map for this frame; the path uses straight segments."),
    JA("このフレームには輪郭マップがないため、パスは直線で結ばれます。"),
    ZH_HANS("此帧没有边缘图，路径使用直线段。"),
    ZH_HANT("此影格沒有邊緣圖，路徑使用直線段。"),
    KO("이 프레임에는 윤곽 맵이 없어 경로가 직선으로 이어집니다."),
    DE("Keine Kantenkarte für dieses Bild; der Pfad verwendet gerade Abschnitte."),
    FR("Pas de carte des contours pour cette image ; le chemin utilise des segments droits."),
    ES("No hay mapa de bordes para este fotograma; el trazado usa segmentos rectos."),
    PT("Sem mapa de contornos para este quadro; o traçado usa segmentos retos."),
    IT("Nessuna mappa dei bordi per questo fotogramma; il tracciato usa segmenti retti."),
    NL("Geen randkaart voor dit frame; het pad gebruikt rechte stukken."),
    RU("Для этого кадра нет карты краёв; контур строится прямыми отрезками."),
    TR("Bu kare için kenar haritası yok; yol düz parçalar kullanır."));

SS_MSG(path_anchors,
    EN("Path anchors: {0}"),
    JA("パスの点: {0}"),
    ZH_HANS("路径锚点：{0}"),
    ZH_HANT("路徑錨點：{0}"),
    KO("패스 앵커: {0}"),
    DE("Pfadanker: {0}"),
    FR("Ancres du chemin : {0}"),
    ES("Anclas del trazado: {0}"),
    PT("Âncoras do traçado: {0}"),
    IT("Ancoraggi del tracciato: {0}"),
    NL("Padankers: {0}"),
    RU("Точек контура: {0}"),
    TR("Yol çapaları: {0}"));

// ===========================================================================
// SAM assist
// ===========================================================================

SS_MSG(sam_first_load,
    EN("Loading the checkpoint. The first prompt of a session takes a few seconds."),
    JA("チェックポイントを読み込み中です。セッション最初のプロンプトは数秒かかります。"),
    ZH_HANS("正在加载检查点。每次会话的第一个提示需要几秒钟。"),
    ZH_HANT("正在載入檢查點。每次工作階段的第一個提示需要幾秒鐘。"),
    KO("체크포인트를 불러오는 중입니다. 세션의 첫 프롬프트는 몇 초 걸립니다."),
    DE("Checkpoint wird geladen. Der erste Prompt einer Sitzung dauert einige Sekunden."),
    FR("Chargement du checkpoint. La première requête d'une session prend quelques secondes."),
    ES("Cargando el checkpoint. La primera indicación de una sesión tarda unos segundos."),
    PT("Carregando o checkpoint. O primeiro prompt de uma sessão leva alguns segundos."),
    IT("Caricamento del checkpoint. Il primo prompt di una sessione richiede alcuni secondi."),
    NL("Checkpoint wordt geladen. De eerste prompt van een sessie duurt een paar seconden."),
    RU("Загрузка контрольной точки. Первый запрос за сеанс занимает несколько секунд."),
    TR("Kontrol noktası yükleniyor. Bir oturumun ilk istemi birkaç saniye sürer."));

SS_MSG(sam_working,
    EN("Segmenting..."),
    JA("セグメント化中…"),
    ZH_HANS("正在分割…"),
    ZH_HANT("正在分割…"),
    KO("분할하는 중…"),
    DE("Wird segmentiert …"),
    FR("Segmentation…"),
    ES("Segmentando…"),
    PT("Segmentando…"),
    IT("Segmentazione…"),
    NL("Bezig met segmenteren…"),
    RU("Сегментация…"),
    TR("Bölütleniyor…"));

}  // namespace maskedit
}  // namespace msg
}  // namespace i18n
}  // namespace spirula

#include "i18n/EndCatalog.h"
