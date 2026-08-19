import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:mobile_sensor_logger/app/app.dart';

void main() {
  testWidgets('HomeScreen renders idle state', (WidgetTester tester) async {
    await tester.pumpWidget(const ProviderScope(child: App()));

    expect(find.text('Idle'), findsOneWidget);
  });
}
