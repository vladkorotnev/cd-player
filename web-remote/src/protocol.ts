/** Matches VirtualKey in firm/lib/espercore/include/esper-core/remote.h. */
export const enum VirtualKey {
  Num0 = 1,
  Num1 = 2,
  Num2 = 3,
  Num3 = 4,
  Num4 = 5,
  Num5 = 6,
  Num6 = 7,
  Num7 = 8,
  Num8 = 9,
  Num9 = 10,
  Delete = 11,
  Dimmer = 12,
  Shuffle = 13,
  Info = 14,
  Program = 15,
  Lyrics = 16,
  Repeat = 17,
  DiscNext = 18,
  DiscPrevious = 19,
  SeekForward = 20,
  SeekBack = 21,
  TrackPrevious = 22,
  TrackNext = 23,
  Display = 24,
  ABRepeat = 25,
  Play = 26,
  Pause = 27,
  Stop = 28,
  Eject = 29,
  ModeSwitch = 30,
  ModeCD = 31,
  ModeRadio = 32,
  ModeBluetooth = 33,
  ModeSettings = 34,
  ModeStandby = 35,
  Up = 36,
  Down = 37,
  Left = 38,
  Right = 39,
  Enter = 40
}

/** Matches ModeSelection in firm/include/mode_host.h. */
export const enum ModeSelection {
  CD = 0,
  Radio = 1,
  Bluetooth = 2,
  Settings = 3,
  Standby = 4
}

export const enum ConnectionState {
  Connecting = 'connecting',
  Connected = 'connected',
  Disconnected = 'disconnected'
}

export const digitKeys: readonly VirtualKey[] = [
  VirtualKey.Num0, VirtualKey.Num1, VirtualKey.Num2, VirtualKey.Num3, VirtualKey.Num4,
  VirtualKey.Num5, VirtualKey.Num6, VirtualKey.Num7, VirtualKey.Num8, VirtualKey.Num9
];

export function isModeSelection(value: unknown): value is ModeSelection {
  return typeof value === 'number' && Number.isInteger(value) &&
    value >= ModeSelection.CD && value <= ModeSelection.Standby;
}
