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

}  // namespace maskedit
}  // namespace msg
}  // namespace i18n
}  // namespace spirula

#include "i18n/EndCatalog.h"
