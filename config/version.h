/**
 * @file version.h
 * @brief ファームウェアの版。STATE の fw_version（上位バイト＝メジャー、下位バイト＝マイナー）。
 *
 * open-questions #8 の決定（2026-11-21）：0x0001（"0.1"）。版の値はここ1か所にだけ置く。
 */
#ifndef CONFIG_VERSION_H
#define CONFIG_VERSION_H

#define FW_VERSION 0x0001u

#endif /* CONFIG_VERSION_H */
