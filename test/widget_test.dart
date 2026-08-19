import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:mobile_sensor_logger/app/app.dart';

void main() {
  testWidgets('opens idle rather than loading', (WidgetTester tester) async {
    await tester.pumpWidget(const ProviderScope(child: App()));

    expect(find.text('Idle'), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.byIcon(Icons.fiber_manual_record), findsOneWidget);
  });

  testWidgets('offers a route into session history', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(const ProviderScope(child: App()));

    expect(find.byIcon(Icons.history), findsOneWidget);
  });
}
