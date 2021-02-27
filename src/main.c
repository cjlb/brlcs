#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <wiringPi.h>

#define LedPin17 0
#define LedPin18 1

#define WATER_LEVEL 0
#define AIR_PRESSURE 1

/* pump control */
#define PUMP_OFF 0
#define PUMP_ON 1
#define DRAIN_OFF 2
#define DRAIN_ON 3
#define COMPRESSOR_OFF 4
#define COMPRESSOR_ON 5

/* control states */
typedef struct
{
	double dblWater;
	double dblAir;
	int bGoNoGo;
	int iNoGoCode;
	int bAbort;
	int iAbortCode;
} ControlState;

ControlState g_actualState;
ControlState g_requestedState;

/* external sensor inputs TBD */
double g_dblWater = 0.0;
double g_dblAir = 0.0;

/* global control widgets */
GtkWidget *g_window;

GtkWidget *g_lvl_water;
GtkWidget *g_lbl_level_water;
GtkWidget *g_lvl_water_requested;
GtkWidget *g_lbl_level_water_requested;

GtkWidget *g_lvl_air;
GtkWidget *g_lbl_level_air;
GtkWidget *g_lvl_air_requested;
GtkWidget *g_lbl_level_air_requested;

GtkWidget *g_btn_launcher;

/* global timer controls */
static gint g_countdown_timer = 0;
static gint g_watchdog_timer = 0;
static int g_bCountdownRunning = FALSE;
static int g_bWatchdogRunning = FALSE;

/* global LED colors */
gpointer g_green;
gpointer g_red;

/* signal handlers */
void on_lvl_water_requested_value_changed();
void on_lvl_air_requested_value_changed();

/* function prototypes */
void start_countdown_timer();
int countdown(int);
void stop_countdown_timer();
void start_watchdog_timer();
void stop_watchdog_timer();
void set_css();
void set_launcher_class(int);
void set_drain_class(int);
gint countdown_callback(gpointer data);

int check_level(int leveltype);
void nogo_or_abort();
void update_actual(int updatetype);
int process_level_change_air(float diff);
int process_level_change_water(float diff);

static void led_on(gpointer data);
static void led_off(gpointer data);

void pump_control(int);
void compressor_control(int);
void launch_control(int, int);

/***********************************************************************************/

/*
 *
 * main entry point
 *
 */
int main(int argc, char *argv[])
{
	GtkBuilder	*builder;

	if (wiringPiSetup() == -1) {
		//when initialize wiringPi failed, print message to screen
		g_print("setup wiringPi failed\n");
		return -1;
	}
	else {
		g_print("setup wiringPi ok\n");
	}

	led_on(g_green);
	led_on(g_red);

	/* set the GPIO mode for the pins we are using */
	pinMode(LedPin17, OUTPUT);
	pinMode(LedPin18, OUTPUT);

	/* initialise our led selectors */
	g_green = GINT_TO_POINTER(0);
	g_red = GINT_TO_POINTER(1);

	/* initialise the GTK system */
	gtk_init(&argc, &argv);

	/* create an instance of the GTK builder */
	builder = gtk_builder_new();

	/* load our glade generated gui definitions into our GTK builder */
	gtk_builder_add_from_file(builder, "glade/window_main.glade", NULL);

	/* connect any signals */
	gtk_builder_connect_signals(builder, NULL);

	/* get global pointers to the various control widgets */
	g_window = GTK_WIDGET(gtk_builder_get_object(builder, "window_main"));
	g_btn_launcher = GTK_WIDGET(gtk_builder_get_object(builder, "btn_launcher"));
	g_lvl_water = GTK_WIDGET(gtk_builder_get_object(builder, "lvl_water_actual"));
	g_lbl_level_water = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_water_actual"));
	g_lvl_water_requested = GTK_WIDGET(gtk_builder_get_object(builder, "lvl_water_requested"));
	g_lbl_level_water_requested = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_water_requested"));

	g_lvl_air = GTK_WIDGET(gtk_builder_get_object(builder, "lvl_air_actual"));
	g_lbl_level_air = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_air_actual"));
	g_lvl_air_requested = GTK_WIDGET(gtk_builder_get_object(builder, "lvl_air_requested"));
	g_lbl_level_air_requested = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_air_requested"));

	/* tidy up the builder */
	g_object_unref(builder);

	/* load our css */
	set_css();

	/* set our launcher button CSS class */
	set_launcher_class(0);

	/* set initial control state */
	g_actualState.dblAir = 0.0;
	g_actualState.dblWater = 0.0;
	g_actualState.iNoGoCode = 0;
	g_actualState.iAbortCode = 0;

	g_requestedState.dblAir = 0.0;
	g_requestedState.dblWater = 0.0;

	g_dblWater = 0.00;
	g_dblAir = 0.00;

	/* set initial launch state to GO */
	nogo_or_abort(FALSE);

	/* set initial values and ranges */
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_lvl_water), 0.00);
	gtk_range_set_value(GTK_RANGE(g_lvl_water_requested), 0.00);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_lvl_air), 0.00);
	gtk_range_set_value(GTK_RANGE(g_lvl_air_requested), 0.00);

	/* start our watchdog timer */
	start_watchdog_timer();

	/* kick off the main loop */
	gtk_widget_show(g_window);
	gtk_main();

	return 0;
}

/*
 *
 * called when window is closed
 *
 */
void on_window_main_destroy()
{
	/* turn off all leds */
	led_off(g_green);
	led_off(g_red);

	/* stop the watchdog timer */
	stop_watchdog_timer();

	/* kill off the gtk main loop */
	gtk_main_quit();
}

/********************************************************************/
/*																																	*/
/* SIGNAL HANDLERS    																							*/
/*																																	*/
/********************************************************************/

/*
 * called when the launch button is clicked
 *
 */
void on_btn_launcher_clicked()
{
	/* turn on the green led */
	led_on(g_green);

	/* set initial countdown value on our luancher button */
	gtk_button_set_label(GTK_BUTTON(g_btn_launcher), " T-10");

	/* kick off the countdown timer */
	start_countdown_timer();
}

/*
 *
 * void on_lvl_water_requested_value_changed();
 *
 * called when water level change is requested
 *
 */
void on_lvl_water_requested_value_changed()
{
	/* note: a change in value here should be picked up by watchdog within 10ms */
	g_requestedState.dblWater = gtk_range_get_value(GTK_RANGE(g_lvl_water_requested));

	gchar *str = g_strdup_printf("%0.2f", g_requestedState.dblWater);
	gtk_label_set_text(GTK_LABEL(g_lbl_level_water_requested), str);
}

/*
 *
 * void on_lvl_air_requested_value_changed();
 *
 * called when air level change is requested
 *
 */
void on_lvl_air_requested_value_changed()
{
	/* note: a change in value here should be picked up by watchdog within 10ms */
	g_requestedState.dblAir = gtk_range_get_value(GTK_RANGE(g_lvl_air_requested));

	gchar *str = g_strdup_printf("%0.2f", g_requestedState.dblAir);
	gtk_label_set_text(GTK_LABEL(g_lbl_level_air_requested), str);
}

/********************************************************************/
/*																	*/
/* CALLBACKS         												*/
/*																	*/
/********************************************************************/

/*
 * watchdog_callback
 *
 * Every 10ms, this will be called to check system_actual against system_requested
 * and reconcile any differences by adding or removing air or water.  Failure to
 * reconcile will result in a NO GO if idle or ABORT if in launch sequence
 * in the clock window.
 */
gint watchdog_callback(gpointer data)
{
	static int iDeltaWater = 0;
	static int iDeltaAir = 0;

	/* simulate update of actual from sensors */
	if (iDeltaWater < 0)
	{
		g_dblWater -= 0.001;
	}
	else
		if (iDeltaWater > 0)
		{
			g_dblWater += 0.001;
		}

	/* show actual water level in progress bar*/
	update_actual(WATER_LEVEL);

	/* now check if we need to adjust water level by looking for */
	/* a difference between actual and requested water level */
	iDeltaWater = check_level(WATER_LEVEL);

	/* simulate update of actual from sensors */
	if (iDeltaAir < 0)
	{
		g_dblAir -= 0.001;
	}
	else
		if (iDeltaAir > 0)
		{
			g_dblAir += 0.001;
		}

	/* show actual air level in progress bar*/
	update_actual(AIR_PRESSURE);

	/* now check if we need to adjust air level by looking for */
	/* a difference between actual and requested water level */
	iDeltaAir = check_level(AIR_PRESSURE);

	return 1; // NOTE: Needs to be non-zero otherwise timer will stop.
}

int countdown(int reset)
{
	static unsigned int countdown = 10;

	if (reset)
	{
		countdown = 10;
	}
	else
	{
		countdown--;
	}

	return countdown;
}

/*
 * countdown_callback
 *
 * Every second, this will be called to update the time
 * in the clock window.
 */
gint countdown_callback(gpointer data)
{
	char str_count[30] = { 0 };

	int count = countdown(FALSE);

	if (count > 0)
	{
		sprintf(str_count, " T-%d", count);
		gtk_button_set_label(GTK_BUTTON(g_btn_launcher), str_count);
	}
	else
	{
		stop_countdown_timer();

		set_launcher_class(1);

		gtk_button_set_label(GTK_BUTTON(g_btn_launcher), "LIFT OFF");

		led_off(g_green);
		led_on(g_red);
	}
	return 1; // NOTE: Needs to be non-zero otherwise timer will stop.
}

/********************************************************************/
/*																	*/
/* HELPERS		    												*/
/*																	*/
/********************************************************************/

void update_actual(int updatetype)
{
	switch (updatetype)
	{
	case 0: // WATER LEVEL
	{
		g_actualState.dblWater = g_dblWater;
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_lvl_water), g_dblWater);

		/* show actual water level in progress bar label */
		gchar *str = g_strdup_printf("%0.2f", g_dblWater);
		gtk_label_set_text(GTK_LABEL(g_lbl_level_water), str);
	}
	break;
	case 1: // AIR PRESSURE
	{
		g_actualState.dblAir = g_dblAir;
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_lvl_air), g_dblAir);

		/* show actual air level in progress bar label */
		gchar *str = g_strdup_printf("%0.2f", g_dblAir);
		gtk_label_set_text(GTK_LABEL(g_lbl_level_air), str);
	}
	break;
	}
}

int check_level(int leveltype)
{
	int iDelta = 0;

	switch (leveltype)
	{
	case 0: // WATER LEVEL
	{
		float req = roundf(((float)g_requestedState.dblWater) * 100) / 100;
		float act = roundf(((float)g_actualState.dblWater) * 100) / 100;

		/* is there a difference? */
		iDelta = process_level_change_water(req - act);
	}
	break;
	case 1: // AIR PRESSURE
	{
		float req = roundf(((float)g_requestedState.dblAir) * 100) / 100;
		float act = roundf(((float)g_actualState.dblAir) * 100) / 100;

		/* is there a difference? */
		iDelta = process_level_change_air(req - act);
	}
	break;
	}
	return iDelta;
}

int process_level_change_water(float diff)
{
	int iDelta = 0; // default pump and drain off
	if (diff > 0.00)
	{
		/* yes, we need more water */
		iDelta = 1; // simulate pump on
		pump_control(DRAIN_OFF);
		pump_control(PUMP_ON);

		/* if countdown is running we need to abort otherwise */
		/* we just set NO GO */
		nogo_or_abort(TRUE);
	}
	else
		if (diff < -0.00)
		{
			/* yes, we need less water */
			iDelta = -1; // simulate drain on
			pump_control(PUMP_OFF);
			pump_control(DRAIN_ON);

			/* if countdown is running we need to abort otherwise */
			/* we just set NO GO */
			nogo_or_abort(TRUE);
		}
		else
		{
			/* we are good to go */
			nogo_or_abort(FALSE);
			pump_control(DRAIN_OFF);
			pump_control(PUMP_OFF);
		}

	return iDelta;
}

int process_level_change_air(float diff)
{
	int iDelta = 0; // default compressor and drain off
	if (diff > 0.00)
	{
		/* yes, we need more air */
		iDelta = 1; // simulate compressor on
		compressor_control(DRAIN_OFF);
		compressor_control(COMPRESSOR_ON);

		/* if countdown is running we need to abort otherwise */
		/* we just set NO GO */
		nogo_or_abort(TRUE);
	}
	else
		if (diff < -0.00)
		{
			/* yes, we need less air */
			iDelta = -1; // simulate drain on
			compressor_control(COMPRESSOR_OFF);
			compressor_control(DRAIN_ON);

			/* if countdown is running we need to abort otherwise */
			/* we just set NO GO */
			nogo_or_abort(TRUE);
		}
		else
		{
			/* we are good to go */
			nogo_or_abort(FALSE);
			compressor_control(DRAIN_OFF);
			compressor_control(COMPRESSOR_OFF);
		}

	return iDelta;
}

void nogo_or_abort(int houston_we_have_a_problem)
{
	static int bGoNoGo = TRUE;
	static int bAbort = TRUE;

	int bCurrentGoNoGo = bGoNoGo;
	int bCurrentAbort = bAbort;

	/* do we have a problem */
	if (houston_we_have_a_problem == FALSE)
	{
		bGoNoGo = TRUE;
		bAbort = FALSE;
	}
	else
	{
		if (g_bCountdownRunning)
		{
			/* we are in launch sequence, so abort */
			bGoNoGo = FALSE;
			bAbort = TRUE;
			stop_countdown_timer();
		}
		else
		{
			/* we are at pre-launch, so no go */
			bGoNoGo = FALSE;
		}
	}

	/* has our state changed */
	if (bAbort != bCurrentAbort || bGoNoGo != bCurrentGoNoGo)
	{
		if (bAbort == FALSE && bGoNoGo == TRUE)
		{
			gtk_button_set_label(GTK_BUTTON(g_btn_launcher), "LAUNCH");
			countdown(TRUE);
		}
		launch_control(bGoNoGo, bAbort);
	}
}

void set_launcher_class(int state)
{
	GtkStyleContext *context;

	/* get the style context for the launcher button */
	context = gtk_widget_get_style_context(GTK_WIDGET(g_btn_launcher));

	/* add or remove the 'launched' css class from the context */
	switch (state)
	{
	case 1:
		gtk_style_context_add_class(context, "launched");
		break;
	default:
		gtk_style_context_remove_class(context, "launched");
		break;
	}
}

void pump_control(int state)
{
	GtkStyleContext *context;

	/* get the style context for the water level label */
	context = gtk_widget_get_style_context(GTK_WIDGET(g_lbl_level_water));

	/* add or remove the 'pump-on' css class from the context */
	switch (state)
	{
	case PUMP_ON:
		gtk_style_context_add_class(context, "pump-on");
		break;
	case DRAIN_ON:
		gtk_style_context_add_class(context, "drain-on");
		break;
	case PUMP_OFF:
		gtk_style_context_remove_class(context, "pump-on");
		break;
	case DRAIN_OFF:
		gtk_style_context_remove_class(context, "drain-on");
		break;
	default:
		gtk_style_context_remove_class(context, "pump-on");
		gtk_style_context_remove_class(context, "drain-on");
		break;
	}
}

void compressor_control(int state)
{
	GtkStyleContext *context;

	/* get the style context for the air level label */
	context = gtk_widget_get_style_context(GTK_WIDGET(g_lbl_level_air));

	/* add or remove the 'pump-on' css class from the context */
	switch (state)
	{
	case COMPRESSOR_ON:
		gtk_style_context_add_class(context, "pump-on");
		break;
	case DRAIN_ON:
		gtk_style_context_add_class(context, "drain-on");
		break;
	case COMPRESSOR_OFF:
		gtk_style_context_remove_class(context, "pump-on");
		break;
	case DRAIN_OFF:
		gtk_style_context_remove_class(context, "drain-on");
		break;
	default:
		gtk_style_context_remove_class(context, "pump-on");
		gtk_style_context_remove_class(context, "drain-on");
		break;
	}
}

/*
 * start_countdown_timer
 *
 * Starts up the countdown timer
 *
 */
void start_countdown_timer()
{
	/* If the timer isn't already running */
	if (!g_bCountdownRunning)
	{
		/* Call function 'TimerCallback' every 1000ms */
		g_countdown_timer = g_timeout_add(1000, countdown_callback, NULL);

		/* Timer is running */
		g_bCountdownRunning = TRUE;
	}
}

/*
 * stop_countdown_timer
 *
 * Stops the countdown timer.
 *
 */
void stop_countdown_timer()
{
	/* if the countdown timer is running */
	if (g_bCountdownRunning)
	{
		/* Stop the timer */
		g_source_remove(g_countdown_timer);

		/* Fix the flag */
		g_bCountdownRunning = FALSE;
	}
}

/*
 * start_watchdog_timer
 *
 * Starts up the watchdog timer
 *
 */
void start_watchdog_timer()
{
	/* if the watchdog timer isn't already running */
	if (!g_bWatchdogRunning)
	{
		/* Call function 'watchdog_callback' every 10ms */
		g_watchdog_timer = g_timeout_add(10, watchdog_callback, NULL);

		/* watchdog is running */
		g_bWatchdogRunning = TRUE;
	}
}

/*
 * stop_watchdog_timer
 *
 * Stops the watchdog timer.
 *
 */
void stop_watchdog_timer()
{
	/* if the watchdog timer is running */
	if (g_bWatchdogRunning)
	{
		/* Stop the watchdog timer */
		g_source_remove(g_watchdog_timer);

		/* Fix the flag */
		g_bWatchdogRunning = FALSE;
	}
}

/*
 * set_css
 *
 * load 'style.css' from our CSS folder into the screen context
 *
 */
void set_css()
{
	GtkCssProvider *css_provider;
	GdkDisplay *display;
	GdkScreen *screen;
	const char *css_file = "css/style.css";
	GError *error = NULL;

	/* instantiate our css provider */
	css_provider = gtk_css_provider_new();

	/* get a pointer to the default display */
	display = gdk_display_get_default();

	/* get a pointer to the default screen in the default display */
	screen = gdk_display_get_default_screen(display);

	/* add our css provider to the screen */
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	/* populate our css provider from our CSS file */
	gtk_css_provider_load_from_file(css_provider, g_file_new_for_path(css_file), &error);

	/* tidy up our css provider */
	g_object_unref(css_provider);
}

/*
 *
 * launch_control
 *
 * enable/disable controls as required by current launch state
 *
 */
void launch_control(int bgonogo, int babort)
{
	if (bgonogo == TRUE)
	{
		gtk_widget_set_sensitive(g_btn_launcher, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive(g_btn_launcher, FALSE);
	}
}

/*
 * led_on
 *
 * set the required output GPIO HIGH
 *
 */
static void led_on(gpointer data)
{
	int color = GPOINTER_TO_INT(data);

	// turn led on
	switch (color)
	{
	case 0: // green
		digitalWrite(LedPin17, HIGH);
		break;
	case 1: // red
		digitalWrite(LedPin18, HIGH);
		break;
	}
}

/*
 * led_off
 *
 * set the required output GPIO LOW
 *
 */
static void led_off(gpointer data)
{
	int color = GPOINTER_TO_INT(data);

	// turn led off
	switch (color)
	{
	case 0: // green
		digitalWrite(LedPin17, LOW);
		break;
	case 1: // red
		digitalWrite(LedPin18, LOW);
		break;
	}
}