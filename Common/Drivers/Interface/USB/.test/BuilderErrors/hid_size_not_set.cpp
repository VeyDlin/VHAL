// expect: ReportSize and ReportCount are set before an Input, Output or Feature item
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>

using Report = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0x01)
		.Usage(0x06)
		.Collection(USBHidCollection::Application)
		.Input(USBHidFlags::Data)
		.EndCollection();
}>;

static_assert(Report::Info.reportCount == 1);
