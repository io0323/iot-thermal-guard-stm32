/**
 * @file thresholds.h
 * @brief しきい値の既定値（0.01℃ 単位）。詳細設計書 第4章。ビルド時に -D で変更できる。
 */
#ifndef CONFIG_THRESHOLDS_H
#define CONFIG_THRESHOLDS_H

#ifndef THRESHOLD_WARN_C100
#define THRESHOLD_WARN_C100 4500
#endif
#ifndef THRESHOLD_CRIT_C100
#define THRESHOLD_CRIT_C100 6000
#endif
#ifndef THRESHOLD_HYST_C100
#define THRESHOLD_HYST_C100 300
#endif

#endif /* CONFIG_THRESHOLDS_H */
