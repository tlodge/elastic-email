<?php
// $Id: elastic_email.inc,v 1.1 2010/12/09 05:35:44 neilotoole Exp $

/**
 * @file
 * Provides the implementation of drupal_mail_wrapper() that directs outgoing
 * messages to the elastic_email_send() function. If message queueing is
 * enabled, then the message is queued to be consumed later at cron time.
 *
 * This code is heavily based on (i.e. copied from) the queue_mail module by
 * Khalid Baheyeldin of 2bits.com.
 */


/**
 * Constant for the Elastic Email web service endpoint URL.
 */
define('ELASTIC_EMAIL_ENDPOINT', 'https://api.elasticemail.com/mailer/send');


/**
 * Wrapper function to use Elastic Email and (optionally) job queue.
 *
 * @param $message array
 *   A standard Drupal message.
 *
 * @return
 *  A status string.
 */
function drupal_mail_wrapper($message) {

  $is_queue_enabled = variable_get(ELASTIC_EMAIL_QUEUE_ENABLED, FALSE);

  // If queueing is available and enabled, queue the message.
  if (function_exists('job_queue_add') && $is_queue_enabled) {

    job_queue_add(
      'elastic_email_send',
      t('Queued message for Elastic Email.'),
      array($message),
      drupal_get_path('module', 'elastic_email') . '/elastic_email.inc');

    return t('Email message queued for delivery via Elastic Email at cron time.');
  }
  else {
    // Otherwise send the message directly.
    elastic_email_send($message);

    return t('Queuing unavailable. Email sent directly via Elastic Email.');
  }
}



/**
 * Email sending function, called from job queue, or directly.
 *
 * @param $message
 *   Standard Drupal email message object.
 *
 * @return
 *   TRUE if message delivered; FALSE otherwise.
 */
function elastic_email_send($message = array()) {

  // If there's no 'from', then use the default site email.
  if (empty($message['from'])) {
    $from = variable_get('site_mail', NULL);
    if (!empty($from)) {
      $message['from'] = $from;
    }
    // If 'from' is still empty this error will be caught by
    // the _elastic_email_send() method.
  }

  // Array to hold the set of email recipients.
  $recipients = array();

  // Parse the various fields that can contain email recipients.
  _elastic_email_parse_recipient($recipients, $message['to']);
  _elastic_email_parse_recipient($recipients, $message['headers']['Cc']);
  _elastic_email_parse_recipient($recipients, $message['headers']['Bcc']);

  // Concatenate recipients to a semi-colon separated string.
  if (count($recipients)) {
    $to = implode('; ', $recipients);
  }

  // Attempt to send the message.
  $result = _elastic_email_send(
    $message['from'],
    NULL,
    $to,
    $message['subject'],
    $message['body']);

  if ($result['error']) {
    // If there's an error, log it.
    watchdog('elastic_email', $result['error']);
  }
  elseif (variable_get(ELASTIC_EMAIL_LOG_SUCCESS, TRUE)) {
    // If success, only log if the ELASTIC_EMAIL_LOG_SUCCESS variable is TRUE.
    watchdog('elastic_email', $result['success']['msg'], NULL, WATCHDOG_ERROR);
  }

  return $result['success'] ? TRUE : FALSE;
}


/**
 * Performs the actual sending of the message to the Elastic Email service. This
 * code was originally based on the'sendElasticEmail' code snippet from the
 * Elastic Email website.
 *
 * The $elastic_username and $api_key variables are generally not required, as
 * they they are specified in the module configuration. However, you may supply
 * these parameters if you wish to override the configuration values.
 *
 * You must provide either the $subject or $body_text parameter. That is, it is
 * not possible to send an empty email.
 *
 * If this method succeeds, an array will be returned with the following
 * elements:
 * - array['success']['tx_id']: the transaction id returned by Elastic Email
 * - array['success']['to']: semi-colon separated list of recipients
 * - array['success']['msg']: user-friendly message
 *
 * If there's an error, an array will be returned with the following element:
 * - array['error'] : The error message returned by Elastic Email, or an error
 * message from this module if a pre-condition was not met (e.g. missing
 * required parameters).
 *
 * @param $from
 *   The 'from' (sender) email address.
 * @param $from_name
 *   (optional) The name of the sender. Defaults to NULL.
 * @param $to
 *   The semi-colon separated list of recipient email addresses.
 * @param $subject
 *   The subject line.
 * @param $body_text
 *   The plain-text body of the message.
 * @param $elastic_username
 *   (optional) The Elastic Email account username (typically the account email
 *   address). If not provided, the value from the module configuration is used.
 * @param $api_key
 *   (optional) The Elastic Email account API key. If not provided, the value
 *   from the module configuration is used.
 *
 * @return array
 *   Returns an array with either a 'success' or 'error' elements. See main
 *   function description for details. Note that the error message text will
 *   have already been passed through t().
 *
 * @todo Perhaps support HTML-based email and attachments?
 */
function _elastic_email_send($from, $from_name = NULL, $to, $subject = '', $body_text = '', $elastic_username = NULL,  $api_key = NULL) {

  if (!$elastic_username) {
    // If no username provided, get it from the module configuration.
    $elastic_username = variable_get(ELASTIC_EMAIL_USERNAME, NULL);
  }

  if (!$api_key) {
    // If no API Key provided, get it from the module configuration.
    $api_key = variable_get(ELASTIC_EMAIL_API_KEY, NULL);
  }

  if (empty($elastic_username) || empty($api_key)) {
    $result['error'] = t('Unable to send email to Elastic Email because username or API key not specified.');
  }
  elseif (empty($from) || empty($to) || (empty($subject) && empty($body_text))) {
    $result['error'] = t('Unable to send email because some required email parameters are empty.');
  }


  if (!$result['error']) {

    // It's necessary to urlencode() each of the data values.
    $data = 'username=' . urlencode($elastic_username);
    $data .= '&api_key=' . urlencode($api_key);

    $data .= '&from=' . urlencode($from);
    $data .= '&reply_to=' . urlencode($from);

    if (!empty($from_name)) {
      $data .= '&from_name=' . urlencode($from_name);
      $data .= '&reply_to_name=' . urlencode($from_name);
    }

    $data .= '&to=' . urlencode($to);
    $data .= '&subject=' . urlencode($subject);

    $data .= '&body_text=' . urlencode($body_text);
    // REVISIT: Need to support HTML at some point.
    //$data .= '&body_html=' . urlencode('<p><em>' . $body_text . '</em></p>');

    $ctx = stream_context_create(array('http' => array('method' => 'post', 'content' => $data)));
    $fp = @fopen(ELASTIC_EMAIL_ENDPOINT, 'rb', FALSE, $ctx);

    // The response should be safe, but call check_plain() for paranoia's sake.
    $response = check_plain(@stream_get_contents($fp));

    if (empty($response)) {
      $result['error'] = t('Error: no response (or empty response) received from Elastic Email service.');
    }
    elseif (strpos($response, 'Error') === 0) {
      $result['error'] = $response;
    }
    else {
      // Message was successfully delivered.
      $result['success']['msg'] = t(
        'Success [@tx_id]; message sent to: @recipients',
        array('@tx_id' => $response, '@recipients' => $to));

      $result['success']['tx_id'] = $response;
      $result['success']['recipients'] = $to;

    }
  }

  return $result;
}


/**
 * Given a comma-delimted list of email addresses in the $to parameter, parse
 * the addresses and add to the $recipients array.
 *
 * @param $recipients array
 *   A passed-by-reference array holding recipient email addresses.
 * @param $to
 *   A comma-delimited list of email addresses.
 */
function _elastic_email_parse_recipient(&$recipients, $to) {

  if (!$to) {
    return;
  }

  // Trim any whitespace
  $to = trim($to);

  if (!empty($to)) {
    // Explode on comma
    $parts = explode(',', $to);
    foreach ($parts as $part) {
      // Again, trim any whitespace
      $part = trim($part);
      if (!empty($part)) {
        $recipients[] = $part;
      }
    }
  }
}
