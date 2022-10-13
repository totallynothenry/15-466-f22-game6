#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>
#include <glm/gtx/rotate_vector.hpp>

#define DEBUG

#ifdef DEBUG
#define LOG(ARGS) std::cout << ARGS << std::endl;
#else
#define LOG(ARGS)
#endif

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");

	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}

void Player::Controls::reset() {
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
	jump.downs = 0;
}


//-----------------------------------------

Game::Game() {
	//player 0 gets first serve
	reset(PLAYER_0);
}


void Game::reset(PlayerType type) {
	LOG("Resetting!");
	//place players in initial positions
	player_0.position.x = 0.0f;
	player_0.position.y = -1.75f;

	player_0.controls.reset();

	player_1.position.x = 0.0f;
	player_1.position.y = 1.75f;

	player_1.controls.reset();

	//place pucks in initial position
	float y = type == PLAYER_0 ? -0.75f : 0.75f;
	for (auto &puck : pucks) {
		puck.position.x = 0.0f;
		puck.position.y = y;
		puck.last_hit = NEUTRAL;
	}
}

Player *Game::spawn_player() {
	switch (next_player) {
	case PLAYER_0:
		player_0.type = PLAYER_0;
		next_player = player_1.type == PLAYER_1 ? NEUTRAL : PLAYER_1;
		return &player_0;
	case PLAYER_1:
		player_1.type = PLAYER_1;
		next_player = player_0.type == PLAYER_0 ? NEUTRAL : PLAYER_0;
		return &player_1;
	default:
		spectators.emplace_back();
		Player &player = spectators.back();
		player.type = NEUTRAL;
		return &player;
	}
}

void Game::remove_player(Player *player) {
	if (player == &player_0) {
		player_0.type = NEUTRAL;
		next_player = PLAYER_0;
		return;
	}

	if (player == &player_1) {
		player_1.type = NEUTRAL;
		next_player = PLAYER_1;
		return;
	}

	bool found = false;
	for (auto pi = spectators.begin(); pi != spectators.end(); ++pi) {
		if (&*pi == player) {
			spectators.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::update_player(Player &p, float elapsed, float y_min, float y_max) {
	//Note: Player movement update is adapted from base code

	glm::vec2 dir = glm::vec2(0.0f, 0.0f);
	if (p.controls.left.pressed) dir.x -= 1.0f;
	if (p.controls.right.pressed) dir.x += 1.0f;
	if (p.controls.down.pressed) dir.y -= 1.0f;
	if (p.controls.up.pressed) dir.y += 1.0f;

	if (dir == glm::vec2(0.0f)) {
		//no inputs: just drift to a stop
		float amt = 1.0f - std::pow(0.5f, elapsed / (PlayerAccelHalflife * 2.0f));
		p.velocity = glm::mix(p.velocity, glm::vec2(0.0f,0.0f), amt);
	} else {
		//inputs: tween velocity to target direction
		dir = glm::normalize(dir);

		float amt = 1.0f - std::pow(0.5f, elapsed / PlayerAccelHalflife);

		//accelerate along velocity (if not fast enough):
		float along = glm::dot(p.velocity, dir);
		if (along < PlayerSpeed) {
			along = glm::mix(along, PlayerSpeed, amt);
		}

		//damp perpendicular velocity:
		float perp = glm::dot(p.velocity, glm::vec2(-dir.y, dir.x));
		perp = glm::mix(perp, 0.0f, amt);

		p.velocity = dir * along + glm::vec2(-dir.y, dir.x) * perp;
	}
	p.position += p.velocity * elapsed;

	//reset 'downs' since controls have been handled:
	p.controls.reset();

	//player/arena collisions:
	if (p.position.x < ArenaMin.x + PlayerRadius) {
		p.position.x = ArenaMin.x + PlayerRadius;
		p.velocity.x = std::abs(p.velocity.x);
	}
	if (p.position.x > ArenaMax.x - PlayerRadius) {
		p.position.x = ArenaMax.x - PlayerRadius;
		p.velocity.x =-std::abs(p.velocity.x);
	}
	if (p.position.y < y_min + PlayerRadius) {
		p.position.y = y_min + PlayerRadius;
		p.velocity.y = std::abs(p.velocity.y);
	}
	if (p.position.y > y_max - PlayerRadius) {
		p.position.y = y_max - PlayerRadius;
		p.velocity.y =-std::abs(p.velocity.y);
	}
}


bool Game::check_collision(Puck &puck, Player const &player) {
	glm::vec2 disp = player.position - puck.position;
	float dist = std::sqrt(glm::length2(disp));
	if (dist > PlayerRadius + PuckRadius) return false;

	//collides!

	//elastic collision with "player_mass >>>> puck_mass"
	glm::vec2 dir = disp / dist;
	glm::vec2 v12 = player.velocity - puck.velocity;
	glm::vec2 delta_v12 = dir * glm::dot(dir, v12);
	//Note: player much heavier than puck, no change in player velocity
	puck.velocity += delta_v12 * 2.0f; // 2*m1 / (m1 + m2) ~ 2 when m1 >>>> m2

	//move puck outside of player
	puck.position = player.position - (PlayerRadius + PuckRadius + 0.01f) * dir;

	return true;
}

void Game::fork_pucks(Puck *root) {
	int cnt = -(NUM_PUCKS - 1) / 2;
	for (auto &puck : pucks) {
		if (&puck == root) continue;

		puck.position = root->position;
		puck.velocity = glm::rotate(root->velocity, glm::radians(PUCK_FAN_ANGLE * cnt));
		puck.last_hit = root->last_hit;

		cnt++;
		if (cnt == 0) cnt++;
	}
}

void Game::update(float elapsed) {
	//no need to update spectators

	bool was_grace = grace_period > 0.0f;
	if (was_grace) {
		grace_period = std::max(0.0f, grace_period - elapsed);
	}

	if (grace_period > 0.0f) {
		return;
	}

	if (was_grace) { // was in grace period, no logner in grace period, reset everything
		reset(to_serve);
	}

	//update players
	update_player(player_0, elapsed, Player0Min, Player0Max);
	update_player(player_1, elapsed, Player1Min, Player1Max);

	{ //update pucks
		//position/velocity update:
		for (auto &puck : pucks) {
			puck.prev_pos = puck.position;
			puck.position += puck.velocity * elapsed;
			//pucks decay velocity above a certain max
			float speed = std::sqrt(glm::length2(puck.velocity));
			if (PuckSpeed < speed) {
				puck.velocity *= (PuckSpeed + (speed - PuckSpeed) * PuckRetain) / speed;
			}
		}

		//collision resolution:
		Puck *collide_puck = nullptr;

		for (auto &puck : pucks) {
			//puck/player collisions:
			if (check_collision(puck, player_0)) {
				puck.last_hit = player_0.type;
				collide_puck = &puck;
				break;
			}
			if (check_collision(puck, player_1)) {
				puck.last_hit = player_1.type;
				collide_puck = &puck;
				break;
			}
		}

		//was a collision, respawn all pucks from the one that collided
		if (collide_puck != nullptr) {
			fork_pucks(collide_puck);
		}

		//puck/arena collisions:
		for (auto &puck : pucks) {
			if (puck.position.x < ArenaMin.x + PuckRadius) {
				puck.position.x = ArenaMin.x + PuckRadius;
				puck.velocity.x = std::abs(puck.velocity.x);
			}
			if (puck.position.x > ArenaMax.x - PuckRadius) {
				puck.position.x = ArenaMax.x - PuckRadius;
				puck.velocity.x = -std::abs(puck.velocity.x);
			}

			//boolean variables indicating puck is outside of {var_name}
			bool x_goal_min = puck.position.x < -GoalRadius + PuckRadius;
			bool x_goal_max = puck.position.x > GoalRadius - PuckRadius;
			bool y_min = puck.position.y < ArenaMin.y + PuckRadius;
			bool y_max = puck.position.y > ArenaMax.y - PuckRadius;

			if (x_goal_min || x_goal_max) {
				//not completely within goal x bounds, check vertical wall collision
				if (y_min) {
					puck.position.y = ArenaMin.y + PuckRadius;
					puck.velocity.y = std::abs(puck.velocity.y);
				}
				if (y_max) {
					puck.position.y = ArenaMax.y - PuckRadius;
					puck.velocity.y = -std::abs(puck.velocity.y);
				}
			}

			if (y_min || y_max) {
				//not completely within arena y bounds, check horizontal goal wall collision
				if (x_goal_min && puck.prev_pos.x >= -GoalRadius + PuckRadius) {
					puck.position.x = -GoalRadius + PuckRadius;
					puck.velocity.x = std::abs(puck.velocity.x);
				}
				if (x_goal_max && puck.prev_pos.x < GoalRadius - PuckRadius) {
					puck.position.x = GoalRadius - PuckRadius;
					puck.velocity.x = -std::abs(puck.velocity.x);
				}
			}

			//yes, the above has the downside of corner collisions teleporting the puck a bit too far.
			//it shouldn't be too noticeable anyway...

			//check if scored
			if (puck.position.y < ArenaMin.y - PuckRadius) {
				handle_scored(&puck, player_1.type);
				break;
			}

			if (puck.position.y > ArenaMax.y + PuckRadius) {
				handle_scored(&puck, player_0.type);
				break;
			}
		}
	}

}

void Game::handle_scored(Puck *scored, PlayerType type) {
	LOG("Goal scored! " << type);

	grace_period = GRACE_PERIOD;

	for (auto &puck : pucks) {
		puck.position = scored->position;
		puck.velocity = glm::vec2(0.0f, 0.0f);
	}

	switch (type) {
	case PLAYER_0:
		player_0.score++;
		to_serve = player_1.type == NEUTRAL ? PLAYER_0 : PLAYER_1;
		break;
	case PLAYER_1:
		player_1.score++;
		to_serve = player_0.type == NEUTRAL ? PLAYER_1 : PLAYER_0;
		break;
	default:
		//if one of the players is not connected, always have puck spawn for other player
		to_serve == player_1.type == NEUTRAL ? PLAYER_0 : PLAYER_1;
	}
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.velocity);
		connection.send(player.type);
		connection.send(player.score);
	};

	//send puck info helper:
	auto send_puck = [&](Puck const &puck) {
		connection.send(puck.position);
		connection.send(puck.velocity);
		connection.send(puck.last_hit);
	};

	connection.send(grace_period);
	send_player(player_0);
	send_player(player_1);
	//puck count:
	for (auto const &puck : pucks) {
		send_puck(puck);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(static_cast< void * >(val), &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	read(&grace_period);

	// read player 0
	read(&player_0.position);
	read(&player_0.velocity);
	read(&player_0.type);
	read(&player_0.score);

	// read player 1
	read(&player_1.position);
	read(&player_1.velocity);
	read(&player_1.type);
	read(&player_1.score);

	for (auto &puck : pucks) {
		read(&puck.position);
		read(&puck.velocity);
		read(&puck.last_hit);
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
