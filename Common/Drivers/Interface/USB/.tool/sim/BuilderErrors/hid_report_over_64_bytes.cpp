// expect: a report is at most 64 bytes
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>

using Report = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0xFF00)
		.Usage(0x01)
		.Collection(USBHidCollection::Application)
		.ReportSize(8)
		.ReportCount(65)
		.Input(USBHidFlags::Data)
		.EndCollection();
}>;

static_assert(Report::Info.reportCount == 1);
