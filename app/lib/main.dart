import 'package:flutter/material.dart';

import 'screens/home_screen.dart';
import 'theme.dart';

void main() {
  runApp(const TapTalkApp());
}

class TapTalkApp extends StatefulWidget {
  const TapTalkApp({super.key});

  @override
  State<TapTalkApp> createState() => _TapTalkAppState();
}

class _TapTalkAppState extends State<TapTalkApp> {
  // TODO: let staff pick Paper, Night or High Contrast in settings.
  final TapTalkThemeMode _mode = TapTalkThemeMode.paper;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'TapTalk',
      debugShowCheckedModeBanner: false,
      theme: buildTheme(_mode),
      darkTheme: buildTheme(TapTalkThemeMode.night),
      home: const HomeScreen(),
    );
  }
}
