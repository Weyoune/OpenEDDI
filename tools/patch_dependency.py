# SPDX-License-Identifier: GPL-3.0-only
"""Use verified name references in the pinned reader; preserve original pools."""
import sys
from pathlib import Path
p=Path(sys.argv[1])/'src/formats/m3d.cpp'
s=p.read_text()
marker='// OpenEDDI: pools may retain unreferenced strings after an append-only edit.'
if marker not in s:
 old='    return db;\n}'
 new='''    // OpenEDDI: pools may retain unreferenced strings after an append-only edit.
    db.referees.clear();
    for (size_t i=0;i<50;++i) db.referees.push_back(pool_string(db.pools[2],le16(db.raw_database_metadata,i*2)));
    db.stadiums.clear();
    for (size_t i=0;i<c.n42;++i) db.stadiums.push_back(pool_string(db.pools[3],le16(db.raw_club_auxiliary_words,i*2)));
    return db;
}'''
 assert s.count(old)==1,'Pinned dependency changed; inspect the reader'
 p.write_text(s.replace(old,new))
# The dependency is included as a subproject rather than the top-level project.
for q in list(Path(sys.argv[1]).rglob('CMakeLists.txt'))+list((Path(sys.argv[1])/'src').rglob('*.cmake')):
    source=q.read_text()
    if '${CMAKE_SOURCE_DIR}' in source:q.write_text(source.replace('${CMAKE_SOURCE_DIR}','${OpenBMH_SOURCE_DIR}'))
p=Path(sys.argv[1])/'src/import/classic.cpp'
s=p.read_text()
old='    split(2, db.referees);\n    split(3, db.stadiums);'
if old in s:
    s=s.replace(old,'''    // OpenEDDI: project referenced slots, not unreferenced name-pool tails.
    for(size_t i=0;i<50;++i) db.referees.push_back(name(2,le16(db.raw_database_metadata,i*2)));
    for(size_t i=0;i<168;++i) db.stadiums.push_back(name(3,le16(db.raw_club_auxiliary_words,i*2)));''')
    p.write_text(s)
