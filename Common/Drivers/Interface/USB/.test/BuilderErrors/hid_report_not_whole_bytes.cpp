// expect: a report is not a whole number of bytes
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>

using Report = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0x01)
		.Usage(0x06)
		.Collection(USBHidCollection::Application)
		.ReportSize(1)
		.ReportCount(12)
		.Input(USBHidFlags::Data)
		.EndCollection();
}>;

static_assert(Report::Info.reportCount == 1);
