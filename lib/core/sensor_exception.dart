/// An error that is worth showing to the user verbatim.
///
/// Engines throw this for conditions the user can act on — a denied permission,
/// location services switched off — so the UI can present [message] directly
/// instead of a raw exception string.
class SensorException implements Exception {
  final String message;

  const SensorException(this.message);

  @override
  String toString() => message;
}
