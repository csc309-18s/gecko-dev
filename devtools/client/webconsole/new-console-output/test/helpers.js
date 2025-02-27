/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const reduxActions = require("devtools/client/webconsole/new-console-output/actions/index");
const { configureStore } = require("devtools/client/webconsole/new-console-output/store");
const { IdGenerator } = require("devtools/client/webconsole/new-console-output/utils/id-generator");
const { stubPackets } = require("devtools/client/webconsole/new-console-output/test/fixtures/stubs/index");
const { getAllMessagesById } = require("devtools/client/webconsole/new-console-output/selectors/messages");
const { getPrefsService } = require("devtools/client/webconsole/new-console-output/utils/prefs");
const prefsService = getPrefsService({});
const { PREFS } = require("devtools/client/webconsole/new-console-output/constants");

/**
 * Prepare actions for use in testing.
 */
function setupActions() {
  // Some actions use dependency injection. This helps them avoid using state in
  // a hard-to-test way. We need to inject stubbed versions of these dependencies.
  const wrappedActions = Object.assign({}, reduxActions);

  const idGenerator = new IdGenerator();
  wrappedActions.messagesAdd = (packets) => {
    return reduxActions.messagesAdd(packets, idGenerator);
  };

  return {
    ...reduxActions,
    messagesAdd: packets => reduxActions.messagesAdd(packets, idGenerator)
  };
}

/**
 * Prepare the store for use in testing.
 */
function setupStore(input = [], {
  storeOptions,
  actions,
  hud,
} = {}) {
  if (!hud) {
    hud = {
      proxy: {
        releaseActor: () => {}
      }
    };
  }
  const store = configureStore(hud, storeOptions);

  // Add the messages from the input commands to the store.
  const messagesAdd = actions
    ? actions.messagesAdd
    : reduxActions.messagesAdd;
  store.dispatch(messagesAdd(input.map(cmd => stubPackets.get(cmd))));

  return store;
}

/**
 * Create deep copy of given packet object.
 */
function clonePacket(packet) {
  return JSON.parse(JSON.stringify(packet));
}

/**
 * Return the message in the store at the given index.
 *
 * @param {object} state - The redux state of the console.
 * @param {int} index - The index of the message in the map.
 * @return {Message} - The message, or undefined if the index does not exists in the map.
 */
function getMessageAt(state, index) {
  const messages = getAllMessagesById(state);
  return messages.get([...messages.keys()][index]);
}

/**
 * Return the first message in the store.
 *
 * @param {object} state - The redux state of the console.
 * @return {Message} - The last message, or undefined if there are no message in store.
 */
function getFirstMessage(state) {
  return getMessageAt(state, 0);
}

/**
 * Return the last message in the store.
 *
 * @param {object} state - The redux state of the console.
 * @return {Message} - The last message, or undefined if there are no message in store.
 */
function getLastMessage(state) {
  const lastIndex = getAllMessagesById(state).size - 1;
  return getMessageAt(state, lastIndex);
}

function getFiltersPrefs() {
  return Object.values(PREFS.FILTER).reduce((res, pref) => {
    res[pref] = prefsService.getBoolPref(pref);
    return res;
  }, {});
}

function clearPrefs() {
  [
    "devtools.hud.loglimit",
    ...Object.values(PREFS.FILTER),
    ...Object.values(PREFS.UI),
  ].forEach(prefsService.clearUserPref);
}

module.exports = {
  clearPrefs,
  clonePacket,
  getFiltersPrefs,
  getFirstMessage,
  getLastMessage,
  getMessageAt,
  prefsService,
  setupActions,
  setupStore,
};
