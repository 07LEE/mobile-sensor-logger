abstract class SensorEngine<T> {
  Stream<T> get stream;

  Future<void> start();

  Future<void> stop();
}
