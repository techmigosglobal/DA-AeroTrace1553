# AeroTrace 1553 0.7.2 bench candidate

0.7.2 updates the operator workspace around the ICD Library and AeroTrace
Assistant while preserving the existing DDC capture and live-monitoring path.

The sidebar presents Device Setup, Monitor and Record, ICD Library, Bus
Analysis, AeroTrace Assistance, Exports and Reports, Preferences, License and
Securities, Help and Diagnostics, and Recover Recording in that order.

Monitor layout controls remain available in Focus view. Movable windows, Tabs,
Tile, and Cascade use the existing channel child windows and saved geometry.

Manual command creation opens the first field editor after a new route is
completed. The route continues to collect subsystem, RT address, subaddress,
direction, payload word count, channel, and bus before field verification and
explicit publication.

AeroTrace Assistant messages support copy, archive, and delete actions from a
right-click menu. Message archive state is migrated and retained in the local
document database. Assistant source retrieval remains separate from manual
mapping publication.

Native CTest passed 43/43 for this build. Provider and x86 helper fixture
checks passed. Physical DDC qualification, installation, upgrade, and silent
installer acceptance remain pending.
