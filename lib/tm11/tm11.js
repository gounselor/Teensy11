// =========== TM11 routines ===========


var tm11 = {
    mts: 0x65, // 17772520 Status Register    6 selr 5 bot 2 wrl 0 tur
    mtc: 0x6080, // 17772522 Command Register   14-13 bpi 7 cu rdy
    mtbrc: 0, // 17772524 Byte Record Counter
    mtcma: 0, // 17772526 Current Memory Address Register
    mtd: 0, // 17772530 Data Buffer Register
    mtrd: 0, // 17772532 TU10 Read Lines
    meta: [] //meta data for drive
};

function tm11_commandEnd() {
    tm11.mts |= 1; // tape unit ready
    tm11.mtc |= 0x80;
    return tm11.mtc & 0x40;
}

function tm11_finish() {
    if (tm11.mtc & 0x40) {
        interrupt(0, 10, 5 << 5, 0224, tm11_commandEnd);
    } else { // if interrupt not enabled just mark completed
        tm11_commandEnd();
    }
}

function tm11_end(err, meta, position, address, count) {
    if (err == 0 && meta.command > 0) {
        if (address == 0 || address > 0x80000000) { // tape mark
            meta.position = position;
            tm11.mts |= 0x4000; // set EOF bit
        } else {
            switch (meta.command) {
                case 1: // read
                    meta.position = position + 2 + ((address + 1) >> 1);
                    meta.command = 0;
                    count = (0x10000 - tm11.mtbrc) & 0xffff;
                    if (count >= address || count == 0) {
                        count = address;
                        tm11.mtbrc = (tm11.mtbrc + count) & 0xffff;
                    } else {
                        tm11.mts |= 0x200; // RLE
                        tm11.mtbrc = 0;
                    }
                    address = ((tm11.mtc & 0x30) << 12) | tm11.mtcma;
                    diskIO(2, meta, position, address, count);
                    // calculate meta.position set count to reduced amount
                    return;
                case 4: // space forward
                    position = position + 2 + ((address + 1) >> 1);
                    meta.position = position;
                    tm11.mtbrc = (tm11.mtbrc + 1) & 0xffff;
                    if (tm11.mtbrc) {
                        diskIO(4, meta, position, 0, 4);
                        return;
                    }
                    break;
                case 5: // space reverse
                    position = position - 4 - ((address + 1) >> 1);
                    meta.position = position;
                    tm11.mtbrc = (tm11.mtbrc + 1) & 0xffff;
                    if (tm11.mtbrc) {
                        if (position > 0) {
                            diskIO(4, meta, position - 2, 0, 4);
                            return;
                        }
                    }
                    break;
                default:
                    panic();
            }
        }
    }
    if (meta.command == 0) {
        tm11.mtbrc = (tm11.mtbrc - count) & 0xffff;
        tm11.mtcma = address & 0xffff;
        tm11.mtc = (tm11.mtc & ~0x30) | ((address >> 12) & 0x30);
    }
    switch (err) {
        case 1: // read error
            tm11.mts |= 0x100; // Bad tape error
            break;
        case 2: // NXM
            tm11.mts |= 0x80; // NXM
            break;
    }
    tm11_finish();
}

function tm11_init() {
    var i;
    tm11.mts = 0x65; //  6 selr 5 bot 2 wrl 0 tur
    tm11.mtc = 0x6080; //  14-13 bpi 7 cu rdy
    for (i = 0; i < 8; i++) {
        if (typeof tm11.meta[i] !== "undefined") {
            tm11.meta[i].position == 0;
        }
    }
}

function tm11_go() {
    var sector, address, count;
    var drive = (tm11.mtc >> 8) & 3;
    tm11.mtc &= ~0x81; // ready bit (7!) and go (0)
    tm11.mts &= 0x04fe; // turn off tape unit ready
    if (typeof tm11.meta[drive] === "undefined") {
        tm11.meta[drive] = {
            "cache": [],
            "blockSize": 65536,
            "postProcess": tm11_end,
            "drive": drive,
            "mapped": 1,
            "maxblock": 0,
            "position": 0,
            "command": 0,
            "url": "tm" + drive + ".tap"
        };
    }
    tm11.meta[drive].command = (tm11.mtc >> 1) & 7;
    //console.log("TM11 Function "+(tm11.meta[drive].command).toString(8)+" "+tm11.mtc.toString(8)+" "+tm11.mts.toString(8)+" @ "+tm11.meta[drive].position.toString(8));
    switch (tm11.meta[drive].command) { // function code
        case 0: // off-line
            break;
        case 1: // read
            diskIO(4, tm11.meta[drive], tm11.meta[drive].position, 0, 4);
            return;
        case 2: // write
        case 3: // write end of file
        case 6: // write with extended IRG
            break;
        case 4: // space forward
            diskIO(4, tm11.meta[drive], tm11.meta[drive].position, 0, 4);
            return;
        case 5: // space reverse
            if (tm11.meta[drive].position > 0) {
                diskIO(4, tm11.meta[drive], tm11.meta[drive].position - 2, 0, 4);
                return;
            }
            break;
        case 7: // rewind
            tm11.meta[drive].position = 0;
            tm11.mts |= 0x20; // set BOT
            break;
        default:
            break;
    }
    tm11_finish();
}

function accessTM11(physicalAddress, data, byteFlag) {
    var result, drive = (tm11.mtc >> 8) & 3;
    switch (physicalAddress & ~1) {
        case 017772520: // tm11.mts
            tm11.mts &= ~0x20; // turn off BOT
            if (typeof tm11.meta[(tm11.mtc >> 8) & 3] !== "undefined") {
                if (tm11.meta[(tm11.mtc >> 8) & 3].position == 0) {
                    tm11.mts |= 0x20; // turn on BOT
                }
            }
            result = tm11.mts;
            break;
        case 017772522: // tm11.mtc
            tm11.mtc &= 0x7fff; // no err bit
            if (tm11.mts & 0xff80) tm11.mtc |= 0x8000;
            result = insertData(tm11.mtc, physicalAddress, data, byteFlag);
            if (data >= 0 && result >= 0) {
                if ((tm11.mtc & 0x40) && !(result & 0x40)) { // if IE being reset then kill any pending interrupts
                    interrupt(1, -1, 5 << 5, 0224);
                }
                if (result & 0x1000) { //init
                    tm11.mts = 0x65; //  6 selr 5 bot 2 wrl 0 tur
                    tm11.mtc = 0x6080; //  14-13 bpi 7 cu rdy
                }
                if ((tm11.mtc & 0x80) && (result & 0x1)) {
                    tm11.mtc = (tm11.mtc & 0x80) | (result & 0xff7f);
                    tm11_go();
                } else {
                    if ((result & 0x40) && (tm11.mtc & 0xc0) == 0x80) {
                        interrupt(1, 10, 5 << 5, 0224);
                    }
                    tm11.mtc = (tm11.mtc & 0x80) | (result & 0xff7f);
                }
            }
            break;
        case 017772524: // tm11.mtbrc
            result = insertData(tm11.mtbrc, physicalAddress, data, byteFlag);
            if (result >= 0) tm11.mtbrc = result;
            break;
        case 017772526: // tm11.mtcma
            result = insertData(tm11.mtcma, physicalAddress, data, byteFlag);
            if (result >= 0) tm11.mtcma = result;
            break;
        case 017772530: // tm11.mtd
        case 017772532: // tm11.mtrd
            result = 0;
            break;
        default:
            CPU.CPU_Error |= 0x10;
            return trap(4, 134);
    }
    //console.log("TM11 Access "+physicalAddress.toString(8)+" "+data.toString(8)+" "+byteFlag.toString(8)+" -> "+result.toString(8));
    return result;
}
