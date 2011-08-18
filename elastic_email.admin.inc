<?php
// $Id: elastic_email.admin.inc,v 1.1 2010/12/09 05:35:44 neilotoole Exp $


/**
 * @file
 * Module configuration code (form builer and AJAX test function callback).
 */


/**
 * Form builder for Elastic Email admin settings form. This consists of:
 * - Elastic Email username field
 * - API key field
 * - Test button (AJAX button for testing username/api key)
 * - Queue messages checkbox
 * - Log success message checkbox
 *
 * @return
 *   The settings form array.
 *
 * @ingroup forms
 */
function elastic_email_settings_form() {

  // Add CSS to make the AJAX part of the form look a little better
  drupal_add_css(drupal_get_path('module', 'elastic_email')
    . "/css/elastic_email.admin.css");

  // Get the webmaster's email address... used below.
  $site_mail = variable_get('site_mail', NULL);

  // Link to online help, used below.
  $help_link = l(t('Administer > Help > Elastic Email'),
    'admin/help/elastic_email');


  // Fieldset to hold credential fields, and Test fieldset
  $form['credentials'] = array(
    '#type' => 'fieldset',
    '#title' => t('Credentials'),
    '#collapsible' => TRUE,
    '#collapsed' => FALSE);

  $form['credentials'][ELASTIC_EMAIL_USERNAME] = array(
    '#type' => 'textfield',
    '#size' => 48,
    '#title' => t('Elastic Email username'),
    '#required' => TRUE,
    '#default_value' => variable_get(ELASTIC_EMAIL_USERNAME, ''),
    '#description' => t('This is typically your Elastic Email account email address.'));

  $form['credentials'][ELASTIC_EMAIL_API_KEY] = array(
    '#type' => 'textfield',
    '#size' => 48,
    '#title' => t('API Key'),
    '#required' => TRUE,
    '#default_value' => variable_get(ELASTIC_EMAIL_API_KEY, ''),
    '#description' => t('The API Key format is typically <tt>xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx</tt>.'));

  $form['credentials']['test'] = array(
    '#type' => 'fieldset',
    '#title' => t('Test'),
    '#collapsible' => FALSE,
    '#collapsed' => FALSE,
    '#attributes' => array('class' => array('credentials-test-fs')),
    '#description' => t(
      'Attempt to send a test email to <em>!site_mail</em> to verify Elastic Email configuration.',
      array('!site_mail' => $site_mail)));

  // Button for testing the credentials
  $form['credentials']['test']['test_config_button'] = array(
    '#type' => 'submit',
    '#value' => t('Test'),
    '#executes_submit_callback' => FALSE,
    '#ajax' => array(
      'path' => 'elastic_email_test_config',
      'wrapper' => 'elastic-email-test-wrapper',
      'effect' => 'fade',
      'method' => 'replace',
      'progress' => array('type' => 'throbber')));

  // DIV to hold the results of the AJAX test call
  $form['credentials']['test']['test_config_button']['#suffix'] =
    '<div id="elastic-email-test-wrapper"></div>';

  // Fieldset for other options
  $form['options'] = array(
    '#type' => 'fieldset',
    '#title' => t('Options'),
    '#collapsible' => TRUE,
    '#collapsed' => FALSE);


  $form['options'][ELASTIC_EMAIL_QUEUE_ENABLED] = array(
    '#type' => 'checkbox',
    '#title' => t('Queue outgoing messages'),
    '#description' => t(
      'When checked, outgoing messages will be queued via job_queue, and delivered when the queue is emptied at cron time. When unchecked, messages are delivered immediately (synchronously). Note that synchronous delivery can cause delay in page execution time.'),
    '#default_value' => variable_get(ELASTIC_EMAIL_QUEUE_ENABLED, FALSE));


  // Check if the job_queue module is available. If it's not, then we must
  // disable (grey-out) the "is queue enabled" checkbox, and display a message.
  if (!function_exists('job_queue_add')) {
    // Disable the checkbox.
    $form['options'][ELASTIC_EMAIL_QUEUE_ENABLED]['#disabled'] = TRUE;

    // Add an explanatory message to the description, including a link to the
    // job_queue module.
    $msg = '<strong>'
      . t('Message queueing is unavailable.')
      . '</strong> <em>'
      . t('To enable queueing, please install and enable the <a href="@job-queue-url">Job Queue</a> module.',
        array('@job-queue-url' => 'http://drupal.org/project/job_queue'))
      . '</em><br />';

    // Prepend the message to the existing description.
    $form['options'][ELASTIC_EMAIL_QUEUE_ENABLED]['#description'] = $msg
      . $form['options'][ELASTIC_EMAIL_QUEUE_ENABLED]['#description'];
  }


  $form['options'][ELASTIC_EMAIL_LOG_SUCCESS] = array(
    '#type' => 'checkbox',
    '#title' => t('Log message delivery success'),
    '#description' => t(
      'When checked, a log message will also be generated for <em>successful</em> email delivery. Errors are always logged.'),
    '#default_value' => variable_get(ELASTIC_EMAIL_LOG_SUCCESS, FALSE));

  // Add the normal settings form stuff
  $form = system_settings_form($form);

  // Get rid of the "Reset to defaults" button, cos we don't like it.
  unset($form['buttons']['reset']);

  // Return the form
  return $form;

}


/**
 * AJAX callback function to test Elastic Email service.
 *
 * This method extracts the username and API key parameters from the POST data,
 * and uses them to send a test message via Elastic Email. The !site_mail
 * email address is used for both sender and recipient of the test mail.
 *
 * @return
 *   HTML 'success' or 'fail' snippet in JSON format
 *
 */
function elastic_email_test_config($form, $form_state) {

  $site_mail = variable_get('site_mail', NULL);

  if (!$site_mail) {
    $result['error'] = t('Unable to send test email because the site e-mail address is not set. This can be specified on the <a href="@site-settings-url">Site information</a> page.',
      array('@site-settings-url' => url('admin/settings/site-information')));
  }

  $username = $_POST[ELASTIC_EMAIL_USERNAME];
  $api_key = $_POST[ELASTIC_EMAIL_API_KEY];

  if (empty($api_key)) {
    $result['error'] = t('You must supply an Elastic Email API key.');
  }

  if (empty($username)) {
    $result['error'] = t('You must supply an Elastic Email username.');
  }


  if (!$result['error']) {
    // Load elastic_email.inc, as it includes _elastic_email_send().
    module_load_include('inc', 'elastic_email');

    $result = _elastic_email_send(
      $site_mail,
      NULL,
      $site_mail,
      t('Elastic Email module: configuration test email'),
      t('This is a test of the Drupal Elastic Email module configuration.')
        . "\n\n"
        . t('Message generated: !time',
            array('!time' => format_date(time(), 'custom', 'r'))),
      $username,
      $api_key);
  }

  if ($result['error']) {
    // There was an error. Return error HTML.
    $data = '<strong class="fail">'
      . t('Failed.')
      . '</strong> '
      . t('Reason:')
      // Directly output the error message as it has already been passed thru
      // t() by _elastic_email_send().
      . ' <tt>' . $result['error'] . '</tt>';
  }
  else {
    // Success!
    $data = '<strong class="success">'
      . t('Success.')
      . '</strong>';
  }
  return $data;
  // This is an AJAX call, so return JSON data.
  //drupal_json(array('status' => TRUE, 'data' => $data));
}
