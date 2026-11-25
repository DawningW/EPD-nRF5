import { buf as crc32 } from 'crc-32';
import { strFromU8, unzipSync } from 'fflate';

const DfuCmd = {
  CREATE: 0x01,
  CALCULATE_CHECKSUM: 0x03,
  EXECUTE: 0x04,
  SELECT: 0x06,
};

const packetSize = 100;

export async function enterDfuMode(device) {
  const service = await (await device.gatt.connect()).getPrimaryService('0000fe59-0000-1000-8000-00805f9b34fb');
  const buttonless = await service.getCharacteristic('8ec90003-f315-4f60-9fb8-838830daea50');
  const disconnected = new Promise((resolve) => device.addEventListener('gattserverdisconnected', resolve, { once: true }));
  await buttonless.startNotifications();
  await buttonless.writeValueWithResponse(Uint8Array.of(1));
  await disconnected;
  await new Promise((resolve) => setTimeout(resolve, 1000));
}

export class Dfu {
  constructor(device, onLog) {
    this.device = device;
    this.onLog = onLog;
  }

  async dfuUpdate(init, firmware, onProgress) {
    const service = await (await this.device.gatt.connect()).getPrimaryService('0000fe59-0000-1000-8000-00805f9b34fb');
    const control = await service.getCharacteristic('8ec90001-f315-4f60-9fb8-838830daea50');
    const packet = await service.getCharacteristic('8ec90002-f315-4f60-9fb8-838830daea50');
    const disconnected = new Promise((resolve) => this.device.addEventListener('gattserverdisconnected', resolve, { once: true }));

    let resolveResponse;
    control.addEventListener('characteristicvaluechanged', (e) => resolveResponse(e.target.value));
    await control.startNotifications();

    const sendControl = async (cmd, data = []) => {
      const response = new Promise((resolve) => { resolveResponse = resolve; });
      await control.writeValueWithResponse(Uint8Array.from([cmd, ...data]));
      const value = await response;
      const result = value.getUint8(2);
      if (result !== 0x01) {
        throw new Error(`0x${result.toString(16)}${result == 0x0B ? '/0x' + value.getUint8(3).toString(16) : ''}`);
      }
      return new DataView(value.buffer, value.byteOffset + 3);
    };

    const transfer = async (type, data, onProgress) => {
      const selected = await sendControl(DfuCmd.SELECT, [type]);
      const maxSize = selected.getUint32(0, true);
      const offset = selected.getUint32(4, true);
      if (offset == data.length) return;
      for (let start = offset - offset % maxSize; start < data.length; start += maxSize) {
        const end = Math.min(start + maxSize, data.length);
        const size = end - start;
        await sendControl(DfuCmd.CREATE, [type, size, size >> 8, size >> 16, size >> 24]);
        for (let i = start; i < end; i += packetSize) {
          const sent = Math.min(i + packetSize, end);
          await packet.writeValueWithResponse(data.subarray(i, sent));
          if (onProgress) onProgress(sent);
        }
        const checksum = await sendControl(DfuCmd.CALCULATE_CHECKSUM);
        if (checksum.getUint32(4, true) !== (crc32(data.subarray(0, end)) >>> 0)) throw new Error('固件校验失败');
        await sendControl(DfuCmd.EXECUTE);
      }
    };

    await transfer(1, init);
    await transfer(2, firmware, onProgress);
    await disconnected;
  }

  async uploadFirmware(file, onProgress) {
    const files = unzipSync(new Uint8Array(await file.arrayBuffer()));
    const manifest = JSON.parse(strFromU8(files['manifest.json'])).manifest;
    const image = manifest.application || manifest.bootloader;
    if (!image) throw new Error('不是有效的固件包');
    const firmware = files[image.bin_file];

    for (let count = 1; count <= 3; count++) {
      try {
        await this.dfuUpdate(files[image.dat_file], firmware, (sent) => {
          onProgress(Math.round(sent * 100 / firmware.length));
        });
        return;
      } catch (e) {
        console.error(e);
        if (e.message) this.onLog('dfu: ' + e.message);
        if (count == 3) throw e;
        this.onLog('  重试 ' + count + '/2');
        await new Promise((resolve) => setTimeout(resolve, 500));
      }
    }
  }
}
