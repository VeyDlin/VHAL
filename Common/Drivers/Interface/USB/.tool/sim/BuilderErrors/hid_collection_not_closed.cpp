// expect: a collection is not closed
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>

using Report = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0x01)
		.Usage(0x06)
		.Collection(USBHidCollection::Application)
		.ReportSize(8)
		.ReportCount(1)
		.Input(USBHidFlags::Data);
}>;

static_assert(Report::Info.reportCount == 1);
