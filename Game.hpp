/**
 * Quantum air hockey
 *
 * Setup:
 * 1. One puck initially (neutral color)
 * 2. Two mallets, one for each player
 * 3. Two goals, one for each player
 * 4. Score counter
 *
 * Mallet movement:
 * 1. WASD keys
 * 2. Cannot go beyond certain line
 *
 * Behavior on hit (by mallet):
 * 1. Splits into N copies, with fanned reflection angles
 * 2. Changes color to mallet's color
 * 3. All existing copies of a puck "collapse" into the observed hit location
 *
 * Behavior on goal:
 * 1. All existing copies of a puck "collapse" into the one that went into the goal
 * 2. Score incremented
 * 3. New pucks spawns
 */
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <list>
#include <random>
#include <string>

#define NUM_PUCKS 5
#define PUCK_FAN_ANGLE 10.0f
#define GRACE_PERIOD 3.0f

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum PlayerType {
	NEUTRAL,
	PLAYER_0,
	PLAYER_1
};

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);

		void reset();
	} controls;

	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	PlayerType type;

	unsigned int score = 0;
};

struct Puck {
	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	//used for wall collisions
	glm::vec2 prev_pos;

	PlayerType last_hit;
};

struct Game {
	std::array< Puck, NUM_PUCKS > pucks;

	Player player_0;
	Player player_1;
	std::list< Player > spectators;

	/**
	 * Game always has 2 players present, connecting assume one of two players if possible
	 * and spectator if not
	 */
	Player *spawn_player(); //adopt of one the two players
	void remove_player(Player *); //release control of one of the two players

	PlayerType next_player = PLAYER_0; //used player spawning
	PlayerType to_serve = PLAYER_0; //used for goal resets
	float grace_period = 0.0f; //grace period after scoring where neither player can move

	Game();

	//state update function:
	void reset(PlayerType type);
	void update_player(Player &p, float elapsed, float y_min, float y_max);
	bool check_collision(Puck &puck, Player const &player);
	void fork_pucks(Puck *);
	void handle_scored(Puck *puck, PlayerType type);
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr float Player0Min = -2.0f;
	inline static constexpr float Player0Max = -0.5f;
	inline static constexpr float Player1Min =  0.5f;
	inline static constexpr float Player1Max =  2.0f;
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-1.0f, Player0Min);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 1.0f, Player1Max);
	inline static constexpr float GoalRadius = 0.27f;
	inline static constexpr glm::vec2 Goal0Center = glm::vec2(0.0, Player0Min);
	inline static constexpr glm::vec2 Goal1Center = glm::vec2(0.0, Player1Max);

	//player constants:
	inline static constexpr float PlayerRadius = 0.09f;
	inline static constexpr float PlayerSpeed = 3.0f;
	inline static constexpr float PlayerAccelHalflife = 0.05f;

	//puck constants:
	inline static constexpr float PuckRadius = 0.05f;
	inline static constexpr float PuckSpeed = 3.0f;
	inline static constexpr float PuckRetain = 0.75f;

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
