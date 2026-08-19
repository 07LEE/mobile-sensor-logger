enum PowerSupplyStatus { charging, discharging, unknown }

class BatterySample {
  final int timestampUs;
  final double percentage;
  final PowerSupplyStatus powerSupplyStatus;

  const BatterySample({
    required this.timestampUs,
    required this.percentage,
    required this.powerSupplyStatus,
  });

  static const csvHeader = ['timestamp_us', 'percentage', 'power_supply_status'];

  List<Object> toCsvRow() => [timestampUs, percentage, powerSupplyStatus.name];
}
