//
// thermal-guard（iot-thermal-guard-stm32）：Renode の STM32DMA に、循環（CIRC）・半分転送（HTIE・HTIF・CHTIF）と、
// テスト用の回数を足したもの。変更した行には「thermal-guard」と書いた（docs/adr/ADR-0006-renode.md）。
//
// 出典：Renode v1.16.1（renode-infrastructure add012af003a0f620d3da52828262676f374d121）
//       src/Emulator/Peripherals/Peripherals/DMA/STM32DMA.cs
// ライセンス：MIT（元のファイルの表示を下に残す。全文は renode/README.md）。
// Renode 本体は変えず、renode/thermal_guard.resc が起動時に読み込んで dma2 を置き換える。
//
// Copyright (c) 2010-2025 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System.Collections.Generic;
using System.Linq;

using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus; // thermal-guard

namespace Antmicro.Renode.Peripherals.DMA
{
    public sealed class STM32DMA_Circular : BasicDoubleWordPeripheral, IKnownSize, IGPIOReceiver, INumberedGPIOOutput
    {
        public STM32DMA_Circular(IMachine machine) : base(machine)
        {
            streams = Enumerable
                .Range(0, NrOfStreams)
                .Select(id => new Stream(this, id))
                .ToArray();

            Connections = Enumerable
                .Range(0, NrOfStreams)
                .ToDictionary(idx => idx, idx => streams[idx].IRQ);

            engine = new DmaEngine(machine.GetSystemBus(this));
            sysbus = machine.GetSystemBus(this); // thermal-guard

            transferCompleteIrqStatus = new IFlagRegisterField[NrOfStreams];
            halfTransferIrqStatus = new IFlagRegisterField[NrOfStreams]; // thermal-guard: 半分転送

            DefineRegisters();
            Reset();
        }

        public override void Reset()
        {
            base.Reset();
            foreach(var stream in streams)
            {
                stream.Reset();
            }
        }

        public void OnGPIO(int number, bool value)
        {
            if(number < 0 || number >= streams.Length)
            {
                this.WarningLog("Attempted to signal DMA stream {0}. Maximum value is {1}", number, streams.Length - 1);
                return;
            }
            streams[number].OnGPIO(value);
        }

        // thermal-guard：Robot のテストが半分転送・全転送の回数を数える（モニターから呼ぶ）
        public ulong GetHalfTransferCount(int stream) => streams[stream].HalfTransferCount;

        public ulong GetTransferCompleteCount(int stream) => streams[stream].TransferCompleteCount;

        public long Size => 0x400;

        public IReadOnlyDictionary<int, IGPIO> Connections { get; }

        private void DefineRegisters()
        {
            var streamRegOffset = new int[] {0, 6, 16, 22};

            var lowInterruptStatusReg = Registers.LowInterruptStatus.Define(this)
                .WithReservedBits(12, 4)
                .WithReservedBits(28, 4);
            var highInterruptStatusReg = Registers.HighInterruptStatus.Define(this)
                .WithReservedBits(12, 4)
                .WithReservedBits(28, 4);
            var lowInterruptClearReg = Registers.LowInterruptClear.Define(this)
                .WithReservedBits(12, 4)
                .WithReservedBits(28, 4);
            var highInterruptClearReg = Registers.HighInterruptClear.Define(this)
                .WithReservedBits(12, 4)
                .WithReservedBits(28, 4);

            for(var streamIdx = 0; streamIdx < NrOfStreams / 2; streamIdx++)
            {
                var lowStreamIdx = streamIdx;
                var offset = streamRegOffset[lowStreamIdx];
                var highStreamIdx = lowStreamIdx + 4;

                lowInterruptStatusReg
                    .WithTaggedFlag($"FEIF{lowStreamIdx}", offset)
                    .WithReservedBits(offset + 1, 1)
                    .WithTaggedFlag($"DMEIF{lowStreamIdx}", offset + 2)
                    .WithTaggedFlag($"TEIF{lowStreamIdx}", offset + 3)
                    .WithFlag(offset + 4, out halfTransferIrqStatus[lowStreamIdx], FieldMode.Read, name: $"HTIF{lowStreamIdx}") // thermal-guard
                    .WithFlag(offset + 5, out transferCompleteIrqStatus[lowStreamIdx], FieldMode.Read, name: $"TCIF{lowStreamIdx}");

                highInterruptStatusReg
                    .WithTaggedFlag($"FEIF{highStreamIdx}", offset)
                    .WithReservedBits(offset + 1, 1)
                    .WithTaggedFlag($"DMEIF{highStreamIdx}", offset + 2)
                    .WithTaggedFlag($"TEIF{highStreamIdx}", offset + 3)
                    .WithFlag(offset + 4, out halfTransferIrqStatus[highStreamIdx], FieldMode.Read, name: $"HTIF{highStreamIdx}") // thermal-guard
                    .WithFlag(offset + 5, out transferCompleteIrqStatus[highStreamIdx], FieldMode.Read, name: $"TCIF{highStreamIdx}");

                lowInterruptClearReg
                    .WithTaggedFlag($"CFEIF{lowStreamIdx}", offset)
                    .WithReservedBits(offset + 1, 1)
                    .WithTaggedFlag($"CDMEIF{lowStreamIdx}", offset + 2)
                    .WithTaggedFlag($"CTEIF{lowStreamIdx}", offset + 3)
                    .WithFlag(offset + 4, FieldMode.Set, name: $"CHTIF{lowStreamIdx}", // thermal-guard
                        writeCallback: (_, value) => ClearIrqFlagOnCondition(halfTransferIrqStatus[lowStreamIdx], value))
                    .WithFlag(offset + 5, FieldMode.Set, name: $"CTCIF{lowStreamIdx}",
                        writeCallback: (_, value) => ClearIrqFlagOnCondition(transferCompleteIrqStatus[lowStreamIdx], value));

                highInterruptClearReg
                    .WithTaggedFlag($"CFEIF{highStreamIdx}", offset)
                    .WithReservedBits(offset + 1, 1)
                    .WithTaggedFlag($"CDMEIF{highStreamIdx}", offset + 2)
                    .WithTaggedFlag($"CTEIF{highStreamIdx}", offset + 3)
                    .WithFlag(offset + 4, FieldMode.Set, name: $"CHTIF{highStreamIdx}", // thermal-guard
                        writeCallback: (_, value) => ClearIrqFlagOnCondition(halfTransferIrqStatus[highStreamIdx], value))
                    .WithFlag(offset + 5, FieldMode.Set, name: $"CTCIF{highStreamIdx}",
                        writeCallback: (_, value) => ClearIrqFlagOnCondition(transferCompleteIrqStatus[highStreamIdx], value));
            }
        }

        private void ClearIrqFlagOnCondition(IFlagRegisterField flag, bool condition)
        {
            if(condition)
            {
                flag.Value = false;
                UpdateInterrupts();
            }
        }

        private void UpdateInterrupts()
        {
            for(var streamId = 0; streamId < NrOfStreams; streamId++)
            {
                var stream = streams[streamId];
                var irqValue = (stream.TransferCompleteIrqEnable && transferCompleteIrqStatus[streamId].Value)
                    || (stream.HalfTransferIrqEnable && halfTransferIrqStatus[streamId].Value); // thermal-guard
                this.NoisyLog("IRQ {0} set to {1}", streamId, irqValue);
                stream.IRQ.Set(irqValue);
            }
        }

        private readonly IFlagRegisterField[] transferCompleteIrqStatus;
        private readonly IFlagRegisterField[] halfTransferIrqStatus; // thermal-guard

        private readonly DmaEngine engine;
        private readonly IBusController sysbus; // thermal-guard
        private readonly Stream[] streams;

        private const int NrOfStreams = 8;
        private const int StreamStep = 0x18;

        private class Stream
        {
            public Stream(STM32DMA_Circular parent, int id)
            {
                this.parent = parent;
                this.id = id;

                DefineRegisters();
                Reset();
            }

            public void Reset()
            {
                dataOffset = 0;
                IRQ.Unset();
            }

            public void OnGPIO(bool value)
            {
                if(!value)
                {
                    return;
                }

                if(!isEnabled.Value)
                {
                    parent.WarningLog("Attempting to perform a transfer on disabled DMA stream {0}. Ignoring request", id);
                    return;
                }

                // Only transfers from peripheral are using interrupt driven approach.
                // In other cases we can just transfer all data immediately.
                if(direction.Value == Direction.PeripheralToMemory)
                {
                    PerformTransfer();
                }
            }

            public IGPIO IRQ { get; } = new GPIO();

            public bool TransferCompleteIrqEnable => transferCompleteIrqEnable.Value;

            public bool HalfTransferIrqEnable => halfTransferIrqEnable.Value; // thermal-guard

            public ulong HalfTransferCount { get; private set; } // thermal-guard：テストで数える

            public ulong TransferCompleteCount { get; private set; } // thermal-guard：テストで数える

            private void DefineRegisters()
            {
                var streamOffset = id * StreamStep;

                (Registers.StreamConfiguration + streamOffset).Define(parent)
                    .WithFlag(0, out isEnabled, name: "EN", writeCallback: (_, value) => HandleEnable(value))
                    .WithTaggedFlag("DMEIE", 1)
                    .WithTaggedFlag("TEIE", 2)
                    .WithFlag(3, out halfTransferIrqEnable, name: "HTIE") // thermal-guard
                    .WithFlag(4, out transferCompleteIrqEnable, name: "TCIE")
                    .WithTaggedFlag("PFCTRL", 5)
                    .WithEnumField(6, 2, out direction, name: "DIR")
                    .WithFlag(8, out circularMode, name: "CIRC") // thermal-guard
                    .WithFlag(9, out peripheralIncrementOffset, name: "PINC")
                    .WithFlag(10, out memoryIncrementOffset, name: "MINC")
                    .WithEnumField(11, 2, out peripheralDataSize, name: "PSIZE")
                    .WithEnumField(13, 2, out memoryDataSize, name: "MSIZE")
                    .WithTaggedFlag("PINCOS", 15)
                    .WithTag("PL", 16, 2)
                    .WithTaggedFlag("DBM", 18)
                    .WithTaggedFlag("CT", 19)
                    .WithTaggedFlag("TRBUFF", 20)
                    .WithTag("PBURST", 21, 2)
                    .WithTag("MBURST", 23, 2)
                    .WithReservedBits(25, 7);

                (Registers.StreamNumberOfData + streamOffset).Define(parent)
                    .WithValueField(0, 16, out nrOfData, name: "NDT")
                    .WithReservedBits(16, 16);

                (Registers.StreamPeripheralAddress + streamOffset).Define(parent)
                    .WithValueField(0, 32, out peripheralAddress, name: "PAR");

                (Registers.StreamMemory0Address + streamOffset).Define(parent)
                    .WithValueField(0, 32, out memory0Address, name: "M0A");

                (Registers.StreamMemory1Address + streamOffset).Define(parent)
                    .WithTag("M1A", 0, 32);

                (Registers.StreamFIFOControl + streamOffset).Define(parent)
                    .WithEnumField(0, 2, out fifoThreshold, name: "FTH")
                    .WithFlag(2, out directMode, name: "DMDIS")
                    .WithTag("FS", 3, 3)
                    .WithReservedBits(6, 1)
                    .WithTaggedFlag("FEIE", 7)
                    .WithReservedBits(8, 24);
            }

            private void HandleEnable(bool value)
            {
                if(value)
                {
                    // thermal-guard：循環で戻す転送数と、半分の位置のために、有効にした時点の NDT を覚える
                    initialNrOfData = nrOfData.Value;
                    dataOffset = 0;
                }
                // Only transfers from peripheral are using interrupt driven approach.
                // In other cases we can just transfer all data immediately.
                if(value && direction.Value != Direction.PeripheralToMemory)
                {
                    PerformTransfer();
                }
            }

            private void PerformTransfer()
            {
                if(CreateRequest() is Request request)
                {
                    var dataUnitSize = direction.Value == Direction.PeripheralToMemory ?
                        PeripheralDataSizeInBytes : MemoryDataSizeInBytes;
                    var nrOfDataUnits = request.Size / dataUnitSize;

                    // thermal-guard：周辺機能からメモリへは、周辺機能のレジスタを 32 ビットで読み、MSIZE の幅で書く。
                    // Renode の多くの周辺機能（STM32_ADC など）は 32 ビットのアクセスだけを受け、PSIZE が半語の読み出しは 0 になる
                    // （実機の ADC_DR は半語で読める）。それ以外の向きは元のまま DmaEngine で写す
                    if(direction.Value == Direction.PeripheralToMemory && request.Size <= 4)
                    {
                        var src = peripheralAddress.Value + (peripheralIncrementOffset.Value ? dataOffset : 0);
                        var dst = memory0Address.Value + (memoryIncrementOffset.Value ? dataOffset : 0);
                        var value = parent.sysbus.ReadDoubleWord(src);
                        switch(memoryDataSize.Value)
                        {
                        case DataSize.Byte:
                            parent.sysbus.WriteByte(dst, (byte)value);
                            break;
                        case DataSize.HalfWord:
                            parent.sysbus.WriteWord(dst, (ushort)value);
                            break;
                        default:
                            parent.sysbus.WriteDoubleWord(dst, value);
                            break;
                        }
                    }
                    else
                    {
                        parent.engine.IssueCopy(request);
                    }
                    nrOfData.Value -= (ulong)nrOfDataUnits;
                    dataOffset += (ulong)request.Size;

                    // thermal-guard：半分転送（NDT が初めの半分になった）
                    if(initialNrOfData >= 2 && nrOfData.Value == initialNrOfData / 2)
                    {
                        parent.halfTransferIrqStatus[id].Value = true;
                        HalfTransferCount++;
                        parent.UpdateInterrupts();
                    }
                    if(nrOfData.Value == 0)
                    {
                        parent.transferCompleteIrqStatus[id].Value = true;
                        TransferCompleteCount++; // thermal-guard
                        dataOffset = 0;
                        if(circularMode.Value)
                        {
                            nrOfData.Value = initialNrOfData; // thermal-guard：循環なら転送数を戻して続ける
                        }
                        parent.UpdateInterrupts();
                    }
                }
                else
                {
                    parent.WarningLog("Error in DMA configuration detected. Ignoring transfer request.");
                }
            }

            private Request? CreateRequest()
            {
                if(nrOfData.Value == 0)
                {
                    parent.WarningLog("Attempting to create DMA request with no data to send. Ignoring operation.");
                    return null;
                }

                if(direction.Value == Direction.Reserved)
                {
                    parent.WarningLog("Attempting to create DMA request with direction set as Reserved. Ignoring operation.");
                    return null;
                }

                ulong sourceAddress;
                TransferType sourceTransferType;
                bool sourceIncrementOffset;

                ulong destinationAddress;
                TransferType destinationTransferType;
                bool destinationIncrementOffset;

                var memoryOffset = memoryIncrementOffset.Value ? dataOffset : 0;
                var peripheralOffset = peripheralIncrementOffset.Value ? dataOffset : 0;

                if(direction.Value == Direction.MemoryToPeripheral)
                {
                    sourceAddress = memory0Address.Value + memoryOffset;
                    sourceTransferType = DataSizeToTransferType(memoryDataSize.Value);
                    sourceIncrementOffset = memoryIncrementOffset.Value;
                    destinationAddress = peripheralAddress.Value + peripheralOffset;
                    destinationTransferType = DataSizeToTransferType(peripheralDataSize.Value);
                    destinationIncrementOffset = peripheralIncrementOffset.Value;
                }
                else
                {
                    // Memory to memory transfers use peripheral address as source address.
                    // Of course the same applies to peripheral to memory transfers.
                    sourceAddress = peripheralAddress.Value + peripheralOffset;
                    sourceTransferType = DataSizeToTransferType(peripheralDataSize.Value);
                    sourceIncrementOffset = peripheralIncrementOffset.Value;
                    destinationAddress = memory0Address.Value + memoryOffset;
                    destinationTransferType = DataSizeToTransferType(memoryDataSize.Value);
                    destinationIncrementOffset = memoryIncrementOffset.Value;
                }

                return new Request(
                    sourceAddress,
                    destinationAddress,
                    GetCurrentTransferSize(),
                    sourceTransferType,
                    destinationTransferType,
                    sourceIncrementOffset,
                    destinationIncrementOffset
                );
            }

            private TransferType DataSizeToTransferType(DataSize dataSize)
            {
                switch(dataSize)
                {
                case DataSize.Byte:
                    return TransferType.Byte;
                case DataSize.HalfWord:
                    return TransferType.Word;
                case DataSize.Word:
                    return TransferType.DoubleWord;
                default:
                    parent.WarningLog("Trying to cast Reserved (0x{0:X}) DataSize to TransferType. Defaulting to Byte.", (int)dataSize);
                    return TransferType.Byte;
                }
            }

            private int GetCurrentTransferSize()
            {
                switch(direction.Value)
                {
                case Direction.MemoryToMemory:
                    return (int)nrOfData.Value * MemoryDataSizeInBytes;
                case Direction.MemoryToPeripheral:
                    return (int)nrOfData.Value * PeripheralDataSizeInBytes;
                case Direction.PeripheralToMemory:
                    return directMode.Value ? FIFOThresholdInBytes : PeripheralDataSizeInBytes;
                default:
                    parent.WarningLog("Trying to get transfer size for Reserved DataSize. Defaulting to 0.");
                    return 0;
                }
            }

            private int FIFOThresholdInBytes => (((int)fifoThreshold.Value + 1) * FIFOSizeInBytes) / 4;

            private int PeripheralDataSizeInBytes => 1 << (int)peripheralDataSize.Value;

            private int MemoryDataSizeInBytes => 1 << (int)memoryDataSize.Value;

            private IFlagRegisterField isEnabled;
            private IFlagRegisterField transferCompleteIrqEnable;
            private IFlagRegisterField halfTransferIrqEnable; // thermal-guard
            private IFlagRegisterField circularMode; // thermal-guard
            private ulong initialNrOfData; // thermal-guard
            private IEnumRegisterField<Direction> direction;
            private IFlagRegisterField peripheralIncrementOffset;
            private IFlagRegisterField memoryIncrementOffset;
            private IEnumRegisterField<DataSize> peripheralDataSize;
            private IEnumRegisterField<DataSize> memoryDataSize;
            private IValueRegisterField nrOfData;
            private IValueRegisterField peripheralAddress;
            private IValueRegisterField memory0Address;
            private IEnumRegisterField<FIFOThreshold> fifoThreshold;
            private IFlagRegisterField directMode;

            private ulong dataOffset;

            private readonly STM32DMA_Circular parent;
            private readonly int id;

            private const int FIFOSizeInBytes = 16;

            private enum Direction
            {
                PeripheralToMemory  = 0x0,
                MemoryToPeripheral  = 0x1,
                MemoryToMemory      = 0x2,
                Reserved            = 0x3,
            }

            private enum DataSize
            {
                Byte        = 0x0,
                HalfWord    = 0x1,
                Word        = 0x2,
                Reserved    = 0x3,
            }

            private enum FIFOThreshold
            {
                OneFourth       = 0x0,
                Half            = 0x1,
                ThreeFourths    = 0x2,
                Full            = 0x3,
            }
        }

        private enum Registers
        {
            LowInterruptStatus      = 0x0,  // DMA_LISR
            HighInterruptStatus     = 0x4,  // DMA_HISR
            LowInterruptClear       = 0x8,  // DMA_LIFCR
            HighInterruptClear      = 0xC,  // DMA_HIFCR
            StreamConfiguration     = 0x10, // DMA_SxCr
            StreamNumberOfData      = 0x14, // DMA_SxNDTR
            StreamPeripheralAddress = 0x18, // DMA_SxPAR
            StreamMemory0Address    = 0x1C, // DMA_SxM0AR
            StreamMemory1Address    = 0x20, // DMA_SxM1AR
            StreamFIFOControl       = 0x24, // DMA_SxFCR
        }
    }
}