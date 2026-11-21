*** Comments ***
Renode のテスト（P6、ADR-0006）。target-renode の .elf を renode/thermal_guard.resc で読み込み、仮想時間で確かめる。
時間の設定は Renode の既定のまま（PerformanceInMips・時間の刻み・TIM3 の 84MHz。速い設定は契約テストのときだけ、#56）。
ADC の開始は TIM2 の更新割り込みからのソフトウェア開始で、実機（TIM2 の TRGO）とは経路が違う（ADC_SOFTWARE_START=1）。

    renode-test tests/renode/thermal_guard.robot

*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Setup          Create Thermal Guard
Test Teardown       Finish Thermal Guard
Resource            ${RENODEKEYWORDS}
Library             ThermalGuardUart.py

*** Variables ***
${RESC}             ${CURDIR}/../../renode/thermal_guard.resc
${ELF}              ${CURDIR}/../../build/target-renode/mcu_c_01.elf
${UART_PORT}        34560
${TIM3_CCR1}        0x40000434
${TEMP_25C_RAW}     2048

*** Keywords ***
Create Thermal Guard
    Execute Command    $elf=@${ELF}
    Execute Script    ${RESC}
    Execute Command    emulation CreateServerSocketTerminal ${UART_PORT} "uart" false
    Execute Command    connector Connect sysbus.usart2 uart
    # 温度は 25℃ のまま（与え続ける）。与えないと生値 0（短絡）として読まれる
    Execute Command    sysbus.adc1 FeedSample ${TEMP_25C_RAW} 0 -1
    Connect Uart    ${UART_PORT}

Finish Thermal Guard
    Log Frames
    Disconnect Uart
    Reset Emulation

Run Seconds With Heartbeat
    [Documentation]    仮想時間で n 秒進める。上位断（10秒）にしないよう、1秒ごとに HEARTBEAT を送る
    [Arguments]    ${seconds}
    FOR    ${i}    IN RANGE    ${seconds}
        Send Heartbeat
        Execute Command    emulation RunFor "00:00:01"
    END
    Drain Uart

Read Register
    [Arguments]    ${address}
    ${text}=    Execute Command    sysbus ReadDoubleWord ${address}
    ${value}=    Monitor Value To Int    ${text}
    RETURN    ${value}

DMA Interrupt Counts
    ${ht}=    Execute Command    sysbus.dma2 GetHalfTransferCount 0
    ${tc}=    Execute Command    sysbus.dma2 GetTransferCompleteCount 0
    ${ht}=    Monitor Value To Int    ${ht}
    ${tc}=    Monitor Value To Int    ${tc}
    RETURN    ${ht}    ${tc}

*** Test Cases ***
起動：最初のフレームが STATE、次の EVENT が BOOT
    Run Seconds With Heartbeat    3
    Frames Should Start With State Then Boot
    Last State Should Be    NORMAL

周期：TELEMETRY と HEARTBEAT が仮想時間で1秒ごと
    Run Seconds With Heartbeat    7
    Periodic Frames Should Be Every Second    TELEMETRY    minimum=5
    Periodic Frames Should Be Every Second    HEARTBEAT    minimum=5

DMA：循環で、半分転送と全転送の割り込みが100ms ごとに交互に来る
    [Documentation]    足した DMA のモデル（renode/peripherals/STM32DMA_Circular.cs）のモードを直接確かめる（#55 ②）。
    ...    ADC は 100Hz、DMA は20語の循環なので、半分（10語）ごとに 100ms で1回の割り込み。
    Run Seconds With Heartbeat    2
    ${ht0}    ${tc0}=    DMA Interrupt Counts
    Should Be True    ${ht0} >= 9 and ${tc0} >= 9    2秒で半分転送 ${ht0} 回・全転送 ${tc0} 回（循環なら1回で止まらない）
    Should Be True    0 <= ${ht0} - ${tc0} <= 1    半分転送と全転送が交互でない：${ht0}・${tc0}
    FOR    ${i}    IN RANGE    6
        ${ha}    ${ta}=    DMA Interrupt Counts
        Execute Command    emulation RunFor "00:00:00.1"
        ${hb}    ${tb}=    DMA Interrupt Counts
        ${dh}=    Evaluate    ${hb} - ${ha}
        ${dt}=    Evaluate    ${tb} - ${ta}
        Should Be True    ${dh} + ${dt} == 1    100ms で割り込みが1回でない（半分転送 +${dh}、全転送 +${dt}）
    END
    ${ndt}=    Read Register    0x40026414
    Should Be True    0 < ${ndt} <= 20    循環なら NDT は 0 で止まらない：${ndt}
    Last State Should Be    NORMAL

指令と PWM：SET_FAN_OVERRIDE 400 に ACK（done・applied 400）、TIM3 の CCR1 が 1344
    Run Seconds With Heartbeat    3
    Last State Should Be    NORMAL
    ${ref}=    Send Set Fan Override    400    60
    Run Seconds With Heartbeat    1
    Ack Should Be    ${ref}    status=1    applied=400
    ${ccr}=    Read Register    ${TIM3_CCR1}
    Should Be Equal As Integers    ${ccr}    1344    CCR1 = 400 × 3360 ÷ 1000

温度と CRITICAL：25℃ から 65℃ まで変化率の上限より遅く上げると CRITICAL、CCR1 が 3360
    Run Seconds With Heartbeat    3
    Last State Should Be    NORMAL
    # 0.5℃ ずつ（変化率の上限 5.00℃ より十分に遅い）。1値は ADC の10回の変換（100ms）分
    ${ramp}=    Ramp Raw Values    25    65    0.5
    FOR    ${raw}    IN    @{ramp}
        Execute Command    sysbus.adc1 FeedSample ${raw} 0 10
    END
    ${hot}=    Raw For Temperature    65
    Execute Command    sysbus.adc1 FeedSample ${hot} 0 -1
    Run Seconds With Heartbeat    14
    Last State Should Be    CRITICAL
    ${ccr}=    Read Register    ${TIM3_CCR1}
    Should Be Equal As Integers    ${ccr}    3360    CRITICAL の下限 1000‰（CCR1 = 3360 > ARR で常に High）

ウォッチドッグ：ループを止めると IWDG がリセットし、STATE → BOOT を送り直す
    [Documentation]    g_renode_hang は target-renode だけにある変数（target-debug・target-release にはない）。
    Run Seconds With Heartbeat    3
    ${mark}=    Frame Count
    ${addr}=    Execute Command    sysbus GetSymbolAddress "g_renode_hang"
    Execute Command    sysbus WriteDoubleWord ${addr} 1
    Run Seconds With Heartbeat    4
    Frames Should Start With State Then Boot    ${mark}

スタック：塗りの残りから求めた最大使用量（stack_peak と同じ計算）
    [Documentation]    #54：塗る範囲は「底から今の SP の 64 バイト下まで」。値は Robot のログに残す（余白 0 の版との比較は ADR-0006）。
    Run Seconds With Heartbeat    3
    ${ref}=    Send Set Fan Override    400    60
    Run Seconds With Heartbeat    2
    ${dump}=    Execute Command    sysbus ReadBytes 0x20000000 4096
    ${peak}=    Stack Peak From Dump    ${dump}
    Log    stack_peak（塗りの残りから）：${peak} バイト    console=yes
    Should Be True    0 < ${peak} < 4096
