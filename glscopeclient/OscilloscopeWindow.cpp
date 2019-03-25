/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of main application window class
 */

#include "glscopeclient.h"
#include "../scopehal/Instrument.h"
#include "OscilloscopeWindow.h"
//#include "../scopehal/AnalogRenderer.h"
//#include "ProtocolDecoderDialog.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initializes the main window
 */
OscilloscopeWindow::OscilloscopeWindow(Oscilloscope* scope, std::string host, int port)
	: m_scope(scope)
	// m_iconTheme(Gtk::IconTheme::get_default())
{
	//Set title
	char title[256];
	snprintf(title, sizeof(title), "Oscilloscope: %s:%d (%s %s, serial %s)",
		host.c_str(),
		port,
		scope->GetVendor().c_str(),
		scope->GetName().c_str(),
		scope->GetSerial().c_str()
		);
	set_title(title);

	//Initial setup
	set_reallocate_redraws(true);
	set_default_size(1280, 800);

	//Add widgets
	CreateWidgets();

	//Set the update timer (1 kHz)
	sigc::slot<bool> slot = sigc::bind(sigc::mem_fun(*this, &OscilloscopeWindow::OnTimer), 1);
	sigc::connection conn = Glib::signal_timeout().connect(slot, 1);

	ArmTrigger(false);
	m_toggleInProgress = false;
	m_pixelsPerSample = 1;
}

/**
	@brief Application cleanup
 */
OscilloscopeWindow::~OscilloscopeWindow()
{
	for(auto w : m_waveformAreas)
		delete w;
}

/**
	@brief Helper function for creating widgets and setting up signal handlers
 */
void OscilloscopeWindow::CreateWidgets()
{
	//Set up window hierarchy
	add(m_vbox);
		m_vbox.pack_start(m_menu, Gtk::PACK_SHRINK);
			m_menu.append(m_fileMenuItem);
				m_fileMenuItem.set_label("File");
				m_fileMenuItem.set_submenu(m_fileMenu);
					auto item = Gtk::manage(new Gtk::MenuItem("Quit", false));
					item->signal_activate().connect(
						sigc::mem_fun(*this, &OscilloscopeWindow::OnQuit));
					m_fileMenu.append(*item);
			m_menu.append(m_channelsMenuItem);
				m_channelsMenuItem.set_label("Channels");
				m_channelsMenuItem.set_submenu(m_channelsMenu);
		m_vbox.pack_start(m_toolbar, Gtk::PACK_SHRINK);
			m_toolbar.append(m_btnStart, sigc::mem_fun(*this, &OscilloscopeWindow::OnStart));
				m_btnStart.set_tooltip_text("Start (normal trigger)");
				m_btnStart.set_icon_name("media-playback-start");
				m_btnStart.set_sensitive(false);
			m_toolbar.append(m_btnStartSingle, sigc::mem_fun(*this, &OscilloscopeWindow::OnStartSingle));
				m_btnStartSingle.set_tooltip_text("Start (single trigger)");
				m_btnStartSingle.set_icon_name("media-skip-forward");
				m_btnStartSingle.set_sensitive(false);
			m_toolbar.append(m_btnStop, sigc::mem_fun(*this, &OscilloscopeWindow::OnStop));
				m_btnStop.set_tooltip_text("Stop trigger");
				m_btnStop.set_icon_name("media-playback-stop");

	//Done adding widgets
	show_all();

	//Create viewers and menu items for all the channels
	for(size_t i=0; i<m_scope->GetChannelCount(); i++)
	{
		auto chan = m_scope->GetChannel(i);
		auto w = new WaveformArea(
			m_scope,
			chan,
			this,
			Gdk::Color(GetDefaultChannelColor(i))
			);
		m_waveformAreas.emplace(w);
		m_vbox.pack_start(*w);

		auto item = Gtk::manage(new Gtk::CheckMenuItem(chan->GetHwname(), false));
		item->signal_activate().connect(
			sigc::bind<WaveformArea*>(sigc::mem_fun(*this, &OscilloscopeWindow::OnToggleChannel), w));
		m_channelsMenu.append(*item);

		//See which channels are currently on
		if(chan->IsEnabled())
		{
			item->set_active();
			w->show();
		}
		else
			w->hide();
	}

	//Status bar has to come at the bottom
	m_vbox.pack_start(m_statusbar, Gtk::PACK_SHRINK);
		m_statusbar.pack_end(m_sampleRateLabel, Gtk::PACK_SHRINK);
		m_sampleRateLabel.set_size_request(75, 1);
		m_statusbar.pack_end(m_sampleCountLabel, Gtk::PACK_SHRINK);
		m_sampleCountLabel.set_size_request(75, 1);
	m_statusbar.show_all();

	m_channelsMenu.show_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Message handlers

void OscilloscopeWindow::OnAutofitHorizontal()
{
	LogDebug("autofit horz\n");

	//
}

void OscilloscopeWindow::OnZoomInHorizontal()
{
	m_pixelsPerSample *= 1.5;

	for(auto w : m_waveformAreas)
		w->queue_draw();
}

void OscilloscopeWindow::OnZoomOutHorizontal()
{
	m_pixelsPerSample /= 1.5;

	for(auto w : m_waveformAreas)
		w->queue_draw();
}

void OscilloscopeWindow::OnQuit()
{
	close();
}

void OscilloscopeWindow::OnToggleChannel(WaveformArea* w)
{
	//We need this guard because set_active() will invoke this handler again!
	if(m_toggleInProgress)
		return;
	m_toggleInProgress = true;

	auto chan = w->GetChannel();

	//TODO: make this more efficient if we have lots of channels?
	auto children = m_channelsMenu.get_children();
	Gtk::CheckMenuItem* menu = NULL;
	for(auto item : children)
	{
		menu = dynamic_cast<Gtk::CheckMenuItem*>(item);
		if(menu == NULL)
			continue;
		if(menu->get_label() == chan->GetHwname())
			break;
	}

	if(w->is_visible())
	{
		w->hide();
		chan->Disable();
		if(menu)
			menu->set_active(false);
	}
	else
	{
		w->show();
		w->GetChannel()->Enable();
		if(menu)
			menu->set_active(true);
	}

	m_toggleInProgress = false;
}

bool OscilloscopeWindow::OnTimer(int /*timer*/)
{
	Oscilloscope::TriggerMode status = m_scope->PollTrigger();
	if(status > Oscilloscope::TRIGGER_MODE_COUNT)
	{
		//Invalid value, skip it
		return true;
	}

	//If not TRIGGERED, do nothing
	if(status != Oscilloscope::TRIGGER_MODE_TRIGGERED)
		return true;

	//double dt = GetTime() - m_tArm;
	//LogDebug("Triggered (trigger was armed for %.2f ms)\n", dt * 1000);

	//Triggered - get the data from each channel
	//double start = GetTime();
	m_scope->AcquireData(sigc::mem_fun(*this, &OscilloscopeWindow::OnCaptureProgressUpdate));
	//dt = GetTime() - start;
	//LogDebug("    Capture downloaded in %.2f ms\n", dt * 1000);

	//Update the status
	UpdateStatusBar();

	//Update the view
	for(auto w : m_waveformAreas)
		w->OnWaveformDataReady();

	//Re-arm trigger for another pass
	if(!m_triggerOneShot)
		ArmTrigger(false);

	//We've stopped
	else
	{
		m_btnStart.set_sensitive(true);
		m_btnStartSingle.set_sensitive(true);
		m_btnStop.set_sensitive(false);
	}

	//false to stop timer
	return true;
}

void OscilloscopeWindow::UpdateStatusBar()
{
	//Find the first enabled channel (assume same config as the rest for now)
	OscilloscopeChannel* chan = NULL;
	for(size_t i=0; i<m_scope->GetChannelCount(); i++)
	{
		chan = m_scope->GetChannel(i);
		if(chan->IsEnabled())
			break;
	}

	double gsps = 1000 / chan->GetData()->m_timescale;
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%.1f GS/s", gsps);
	m_sampleRateLabel.set_label(tmp);

	size_t len = chan->GetData()->GetDepth();
	if(len > 1e6)
		snprintf(tmp, sizeof(tmp), "%.0f MS", len * 1e-6f);
	else if(len > 1e3)
		snprintf(tmp, sizeof(tmp), "%.0f kS", len * 1e-3f);
	else
		snprintf(tmp, sizeof(tmp), "%zu S", len);
	m_sampleCountLabel.set_label(tmp);
}

void OscilloscopeWindow::OnStart()
{
	m_btnStart.set_sensitive(false);
	m_btnStartSingle.set_sensitive(false);
	m_btnStop.set_sensitive(true);

	ArmTrigger(false);
}

void OscilloscopeWindow::OnStartSingle()
{
	m_btnStart.set_sensitive(false);
	m_btnStartSingle.set_sensitive(false);
	m_btnStop.set_sensitive(true);

	ArmTrigger(true);
}

void OscilloscopeWindow::OnStop()
{
	m_btnStart.set_sensitive(true);
	m_btnStartSingle.set_sensitive(true);
	m_btnStop.set_sensitive(false);

	m_scope->Stop();
	m_triggerOneShot = true;
}

void OscilloscopeWindow::ArmTrigger(bool oneshot)
{
	m_scope->StartSingleTrigger();
	m_triggerOneShot = oneshot;
	m_tArm = GetTime();
}


/*
void OscilloscopeWindow::OnZoomOut()
{
	//Get center of current view
	float fract = m_viewscroller.get_hadjustment()->get_value() / m_viewscroller.get_hadjustment()->get_upper();

	//Change zoom
	m_timescale /= 1.5;
	OnZoomChanged();

	//Dispatch the draw events
	while(Gtk::Main::events_pending())
		Gtk::Main::iteration();

	//Re-scroll
	m_viewscroller.get_hadjustment()->set_value(fract * m_viewscroller.get_hadjustment()->get_upper());
}

void OscilloscopeWindow::OnZoomIn()
{
	//Get center of current view
	float fract = m_viewscroller.get_hadjustment()->get_value() / m_viewscroller.get_hadjustment()->get_upper();

	//Change zoom
	m_timescale *= 1.5;
	OnZoomChanged();

	//Dispatch the draw events
	while(Gtk::Main::events_pending())
		Gtk::Main::iteration();

	//Re-scroll
	m_viewscroller.get_hadjustment()->set_value(fract * m_viewscroller.get_hadjustment()->get_upper());
}

void OscilloscopeWindow::OnZoomFit()
{
	if( (m_scope->GetChannelCount() != 0) && (m_scope->GetChannel(0) != NULL) && (m_scope->GetChannel(0)->GetData() != NULL))
	{
		CaptureChannelBase* capture = m_scope->GetChannel(0)->GetData();
		int64_t capture_len = capture->m_timescale * capture->GetEndTime();
		m_timescale = static_cast<float>(m_viewscroller.get_width()) / capture_len;
	}

	OnZoomChanged();
}

void OscilloscopeWindow::OnZoomChanged()
{
	for(size_t i=0; i<m_scope->GetChannelCount(); i++)
		m_scope->GetChannel(i)->m_timescale = m_timescale;

	m_view.SetSizeDirty();
	m_view.queue_draw();
}
*/
int OscilloscopeWindow::OnCaptureProgressUpdate(float /*progress*/)
{
	//Dispatch pending gtk events (such as draw calls)
	while(Gtk::Main::events_pending())
		Gtk::Main::iteration();

	return 0;
}
