/* Oracle Redo OpCode: 5.1
   Copyright (C) 2018-2020 Adam Leszczynski.

This file is part of Open Log Replicator.

Open Log Replicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

Open Log Replicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with Open Log Replicator; see the file LICENSE.txt  If not see
<http://www.gnu.org/licenses/>.  */

#include <iostream>
#include <iomanip>
#include "OpCode0501.h"
#include "OracleEnvironment.h"
#include "RedoLogRecord.h"

using namespace std;

namespace OpenLogReplicator {

    OpCode0501::OpCode0501(OracleEnvironment *oracleEnvironment, RedoLogRecord *redoLogRecord) :
            OpCode(oracleEnvironment, redoLogRecord) {

        uint32_t fieldPos = redoLogRecord->fieldPos;
        for (uint32_t i = 1; i <= redoLogRecord->fieldCnt && i <= 2; ++i) {
            uint16_t fieldLength = oracleEnvironment->read16(redoLogRecord->data + redoLogRecord->fieldLengthsDelta + i * 2);
            if (i == 2) {
                if (fieldLength < 8) {
                    oracleEnvironment->dumpStream << "ERROR: too short field ktub: " << dec << fieldLength << endl;
                    return;
                }

                redoLogRecord->objn = oracleEnvironment->read32(redoLogRecord->data + fieldPos + 0);
                redoLogRecord->objd = oracleEnvironment->read32(redoLogRecord->data + fieldPos + 4);
            }
            fieldPos += (fieldLength + 3) & 0xFFFC;
        }
    }

    OpCode0501::~OpCode0501() {
    }

    void OpCode0501::process() {
        OpCode::process();
        uint8_t *colNums, *nulls = nullptr, bits = 1;
        uint32_t fieldPos = redoLogRecord->fieldPos;
        for (uint32_t i = 1; i <= redoLogRecord->fieldCnt; ++i) {
            uint16_t fieldLength = oracleEnvironment->read16(redoLogRecord->data + redoLogRecord->fieldLengthsDelta + i * 2);
            if (i == 1) {
                ktudb(fieldPos, fieldLength);
            } else if (i == 2) {
                ktub(fieldPos, fieldLength);
            } else if (i > 2 && (redoLogRecord->flg & (FLG_MULTIBLOCKUNDOHEAD | FLG_MULTIBLOCKUNDOTAIL | FLG_MULTIBLOCKUNDOMID)) != 0) {
                //incomplete data
            } else if (i == 3) {
                if (redoLogRecord->opc == 0x0A16 || redoLogRecord->opc == 0x0B01) {
                    ktbRedo(fieldPos, fieldLength);
                }
            } else if (i == 4) {
                if (redoLogRecord->opc == 0x0B01) {
                    kdoOpCode(fieldPos, fieldLength);
                    nulls = redoLogRecord->data + redoLogRecord->nullsDelta;

                    if (oracleEnvironment->dumpLogFile >= 1) {
                        if ((redoLogRecord->op & 0x1F) == OP_QMD) {
                            for (uint32_t i = 0; i < redoLogRecord->nrow; ++i)
                                oracleEnvironment->dumpStream << "slot[" << i << "]: " << dec << oracleEnvironment->read16(redoLogRecord->data+redoLogRecord->slotsDelta + i * 2) << endl;
                        }
                    }
                }

            } else if ((redoLogRecord->op & 0x1F) == OP_URP) {
                if (i == 5) {
                    redoLogRecord->colNumsDelta = fieldPos;
                    colNums = redoLogRecord->data + redoLogRecord->colNumsDelta;
                } else if ((redoLogRecord->flags & FLAGS_KDO_KDOM2) != 0) {
                    if (i == 6) {
                        if (oracleEnvironment->dumpLogFile >= 1)
                            dumpColsVector(redoLogRecord->data + fieldPos, oracleEnvironment->read16(colNums), fieldLength);
                    } else if (i == 7) {
                        suppLog(fieldPos, fieldLength);
                    }
                } else {
                    if (i > 5 && i <= 5 + (uint32_t)redoLogRecord->cc) {
                        if (oracleEnvironment->dumpLogFile >= 1) {
                            dumpCols(redoLogRecord->data + fieldPos, oracleEnvironment->read16(colNums), fieldLength, *nulls & bits);
                            colNums += 2;
                            bits <<= 1;
                            if (bits == 0) {
                                bits = 1;
                                ++nulls;
                            }
                        }
                    } else if (i == 6 + (uint32_t)redoLogRecord->cc) {
                        suppLog(fieldPos, fieldLength);
                    }
                }
            } else if ((redoLogRecord->op & 0x1F) == OP_DRP) {
                if (i == 5)
                    suppLog(fieldPos, fieldLength);
            } else if ((redoLogRecord->op & 0x1F) == OP_IRP || (redoLogRecord->op & 0x1F) == OP_ORP) {
                if (i > 4 && i <= 4 + (uint32_t)redoLogRecord->cc) {
                    if (nulls == nullptr) {
                        cerr << "ERROR: nulls = null" << endl;
                        return;
                    }
                    if (oracleEnvironment->dumpLogFile >= 1) {
                        dumpCols(redoLogRecord->data + fieldPos, i - 5, fieldLength, *nulls & bits);
                        bits <<= 1;
                        if (bits == 0) {
                            bits = 1;
                            ++nulls;
                        }
                    }
                } else if (i == 5 + (uint32_t)redoLogRecord->cc) {
                    suppLog(fieldPos, fieldLength);
                }

            } else if ((redoLogRecord->op & 0x1F) == OP_QMI) {
                if (i == 5) {
                    redoLogRecord->rowLenghsDelta = fieldPos;
                } else if (i == 6) {
                    if (oracleEnvironment->dumpLogFile >= 1) {
                        dumpRows(redoLogRecord->data + fieldPos);
                    }
                }

            } else if ((redoLogRecord->op & 0x1F) == OP_CFA) {
                if (i == 5) {
                    suppLog(fieldPos, fieldLength);
                }
            }

            fieldPos += (fieldLength + 3) & 0xFFFC;
        }
    }

    const char* OpCode0501::getUndoType() {
        return "";
    }

    void OpCode0501::ktudb(uint32_t fieldPos, uint32_t fieldLength) {
        if (fieldLength < 20) {
            oracleEnvironment->dumpStream << "too short field ktudb: " << dec << fieldLength << endl;
            return;
        }

        redoLogRecord->xid = XID(oracleEnvironment->read16(redoLogRecord->data + fieldPos + 8),
                oracleEnvironment->read16(redoLogRecord->data + fieldPos + 10),
                oracleEnvironment->read32(redoLogRecord->data + fieldPos + 12));

        if (oracleEnvironment->dumpLogFile >= 1) {
            uint16_t siz = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 0);
            uint16_t spc = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 2);
            uint16_t flgKtudb = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 4);
            uint16_t seq = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 16);
            uint8_t rec = redoLogRecord->data[fieldPos + 18];

            oracleEnvironment->dumpStream << "ktudb redo:" <<
                    " siz: " << dec << siz <<
                    " spc: " << dec << spc <<
                    " flg: 0x" << setfill('0') << setw(4) << hex << flgKtudb <<
                    " seq: 0x" << setfill('0') << setw(4) << seq <<
                    " rec: 0x" << setfill('0') << setw(2) << (uint32_t)rec << endl;
            oracleEnvironment->dumpStream << "           " <<
                    " xid:  " << PRINTXID(redoLogRecord->xid) << "  " << endl;
        }
    }

    void OpCode0501::suppLog(uint32_t fieldPos, uint32_t fieldLength) {
        if (fieldLength < 20) {
            oracleEnvironment->dumpStream << "ERROR: too short supplemental log: " << dec << fieldLength << endl;
            return;
        }

        redoLogRecord->suppLogType = redoLogRecord->data[fieldPos + 0];
        redoLogRecord->suppLogFb = redoLogRecord->data[fieldPos + 1];
        redoLogRecord->suppLogCC = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 2);
        redoLogRecord->suppLogBefore = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 6);
        redoLogRecord->suppLogAfter = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 8);

        if (oracleEnvironment->dumpLogFile >= 2) {
            oracleEnvironment->dumpStream <<
                    "supp log type: " << dec << (uint32_t)redoLogRecord->suppLogType <<
                    " fb: " << dec << (uint32_t)redoLogRecord->suppLogFb <<
                    " cc: " << dec << redoLogRecord->suppLogCC <<
                    " before: " << dec << redoLogRecord->suppLogBefore <<
                    " after: " << dec << redoLogRecord->suppLogAfter << endl;
        }

        if (fieldLength >= 26) {
            redoLogRecord->suppLogBdba = oracleEnvironment->read32(redoLogRecord->data + fieldPos + 20);
            redoLogRecord->suppLogSlot = oracleEnvironment->read16(redoLogRecord->data + fieldPos + 24);
            oracleEnvironment->dumpStream <<
                    "supp log bdba: 0x" << setfill('0') << setw(8) << hex << redoLogRecord->suppLogBdba <<
                    "." << hex << redoLogRecord->suppLogSlot << endl;
        }
    }
}
