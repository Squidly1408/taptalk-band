import 'package:flutter/material.dart';

/// Brand colours (see brand/README.md).
class TapTalkColors {
  static const sunnyYellow = Color(0xFFFFC93C);
  static const deepOcean = Color(0xFF1B3A57);
  static const calmTeal = Color(0xFF2BB3A3);
  static const coral = Color(0xFFFF6F59);
  static const paper = Color(0xFFFAF7F0);
  static const slate = Color(0xFF4A6278);
}

enum TapTalkThemeMode { paper, night, highContrast }

/// Atkinson Hyperlegible is the brand font. Add the font files to the app's
/// assets and declare them in pubspec.yaml to use it; until then Flutter falls
/// back to the platform font.
const _fontFamily = 'AtkinsonHyperlegible';

ThemeData buildTheme(TapTalkThemeMode mode) {
  switch (mode) {
    case TapTalkThemeMode.paper:
      return ThemeData(
        useMaterial3: true,
        fontFamily: _fontFamily,
        colorScheme: ColorScheme.fromSeed(
          seedColor: TapTalkColors.calmTeal,
          primary: TapTalkColors.calmTeal,
          secondary: TapTalkColors.sunnyYellow,
          error: TapTalkColors.coral,
          surface: TapTalkColors.paper,
          onSurface: TapTalkColors.deepOcean,
        ),
        scaffoldBackgroundColor: TapTalkColors.paper,
        appBarTheme: const AppBarTheme(
          backgroundColor: TapTalkColors.sunnyYellow,
          foregroundColor: TapTalkColors.deepOcean,
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(minimumSize: const Size(64, 52)),
        ),
      );
    case TapTalkThemeMode.night:
      return ThemeData(
        useMaterial3: true,
        fontFamily: _fontFamily,
        colorScheme: ColorScheme.fromSeed(
          seedColor: TapTalkColors.calmTeal,
          brightness: Brightness.dark,
          primary: TapTalkColors.calmTeal,
          secondary: TapTalkColors.sunnyYellow,
          error: TapTalkColors.coral,
        ),
      );
    case TapTalkThemeMode.highContrast:
      return ThemeData(
        useMaterial3: true,
        fontFamily: _fontFamily,
        colorScheme: const ColorScheme.highContrastLight(
          primary: Colors.black,
          secondary: Colors.black,
          surface: Colors.white,
          onSurface: Colors.black,
        ),
      );
  }
}
