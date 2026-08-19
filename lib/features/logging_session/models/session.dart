import 'session_status.dart';

class Session {
  final String id;
  final DateTime startTime;
  final String directoryPath;
  final SessionStatus status;
  final DateTime? endTime;

  const Session({
    required this.id,
    required this.startTime,
    required this.directoryPath,
    required this.status,
    this.endTime,
  });

  Session copyWith({SessionStatus? status, DateTime? endTime}) {
    return Session(
      id: id,
      startTime: startTime,
      directoryPath: directoryPath,
      status: status ?? this.status,
      endTime: endTime ?? this.endTime,
    );
  }
}
