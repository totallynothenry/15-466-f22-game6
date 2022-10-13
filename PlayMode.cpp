#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

PlayMode::PlayMode(Client &client_) : client(client_) {
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.keysym.sym == SDLK_a) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			controls.jump.downs += 1;
			controls.jump.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			controls.jump.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	controls.send_controls_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);
}

inline glm::u8vec4 get_color(PlayerType type) {
	switch (type) {
	case PLAYER_0:
		return glm::u8vec4(0xff, 0x0, 0x0, 0xff); // Red
	case PLAYER_1:
		return glm::u8vec4(0x0, 0x0, 0xff, 0xff); // Blue
	default:
		//Assume NEUTRAL
		return glm::u8vec4(0xff, 0xff, 0xff, 0xff); // White
	}
}


void PlayMode::draw(glm::uvec2 const &drawable_size) {

	static std::array< glm::vec2, 16 > const circle = [](){
		std::array< glm::vec2, 16 > ret;
		for (uint32_t a = 0; a < ret.size(); ++a) {
			float ang = a / float(ret.size()) * 2.0f * float(M_PI);
			ret[a] = glm::vec2(std::cos(ang), std::sin(ang));
		}
		return ret;
	}();

	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	//figure out view transform to center the arena:
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	float scale = std::min(
		2.0f * aspect / (Game::ArenaMax.x - Game::ArenaMin.x + 2.0f * Game::PlayerRadius),
		2.0f / (Game::ArenaMax.y - Game::ArenaMin.y + 2.0f * Game::PlayerRadius)
	);
	glm::vec2 offset = -0.5f * (Game::ArenaMax + Game::ArenaMin);

	glm::mat4 world_to_clip = glm::mat4(
		scale / aspect, 0.0f, 0.0f, offset.x,
		0.0f, scale, 0.0f, offset.y,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	{
		DrawLines lines(world_to_clip);

		//helper:
		auto draw_text = [&](glm::vec2 const &at, std::string const &text, float H) {
			lines.draw_text(text,
				glm::vec3(at.x, at.y, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = (1.0f / scale) / drawable_size.y;
			lines.draw_text(text,
				glm::vec3(at.x + ofs, at.y + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		};

		(void)draw_text;

		static constexpr glm::u8vec4 purple = glm::u8vec4(0xff, 0x00, 0xff, 0xff);
		static constexpr glm::u8vec4 yellow = glm::u8vec4(0xff, 0xe8, 0x00, 0xff);
		static constexpr glm::u8vec4 white = glm::u8vec4(0xff, 0xff, 0xff, 0xff);

		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), purple);
		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), purple);

		//center line
		lines.draw(glm::vec3(Game::ArenaMin.x, 0.0f, 0.0f), glm::vec3(Game::ArenaMax.x, 0.0f, 0.0f), yellow);

		//neutral zone
		lines.draw(glm::vec3(Game::ArenaMin.x, Game::Player0Max, 0.0f), glm::vec3(Game::ArenaMax.x, Game::Player0Max, 0.0f), white);
		lines.draw(glm::vec3(Game::ArenaMin.x, Game::Player1Min, 0.0f), glm::vec3(Game::ArenaMax.x, Game::Player1Min, 0.0f), white);

		//goals
		lines.draw(glm::vec3(-Game::GoalRadius, Game::ArenaMin.y, 0.0f), glm::vec3(-Game::GoalRadius, Game::ArenaMin.y - 0.1f, 0.0f), white);
		lines.draw(glm::vec3( Game::GoalRadius, Game::ArenaMin.y, 0.0f), glm::vec3( Game::GoalRadius, Game::ArenaMin.y - 0.1f, 0.0f), white);
		lines.draw(glm::vec3(-Game::GoalRadius, Game::ArenaMax.y, 0.0f), glm::vec3(-Game::GoalRadius, Game::ArenaMax.y + 0.1f, 0.0f), white);
		lines.draw(glm::vec3( Game::GoalRadius, Game::ArenaMax.y, 0.0f), glm::vec3( Game::GoalRadius, Game::ArenaMax.y + 0.1f, 0.0f), white);
		for (uint32_t a = 0; a < circle.size(); ++a) {
			glm::vec2 pos1 = Game::Goal0Center + Game::GoalRadius * circle[a];
			glm::vec2 pos2 = Game::Goal0Center + Game::GoalRadius * circle[(a+1)%circle.size()];
			if (pos1.y < Game::ArenaMin.y || pos2.y < Game::ArenaMin.y) continue;
			lines.draw(glm::vec3(pos1, 0.0f), glm::vec3(pos2, 0.0f), white);
		}
		for (uint32_t a = 0; a < circle.size(); ++a) {
			glm::vec2 pos1 = Game::Goal1Center + Game::GoalRadius * circle[a];
			glm::vec2 pos2 = Game::Goal1Center + Game::GoalRadius * circle[(a+1)%circle.size()];
			if (pos1.y > Game::ArenaMax.y || pos2.y > Game::ArenaMax.y) continue;
			lines.draw(glm::vec3(pos1, 0.0f), glm::vec3(pos2, 0.0f), white);
		}


		lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), purple);
		lines.draw(glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), purple);

		//draw player 0
		glm::u8vec4 col_0 = get_color(game.player_0.type);
		for (uint32_t a = 0; a < circle.size(); ++a) {
			lines.draw(
				glm::vec3(game.player_0.position + Game::PlayerRadius * circle[a], 0.0f),
				glm::vec3(game.player_0.position + Game::PlayerRadius * circle[(a+1)%circle.size()], 0.0f),
				col_0
			);
		}
		draw_text(glm::vec2(0.8f, -0.35f), std::to_string(game.player_0.score), 0.1f);

		//draw player 1
		glm::u8vec4 col_1 = get_color(game.player_1.type);
		for (uint32_t a = 0; a < circle.size(); ++a) {
			lines.draw(
				glm::vec3(game.player_1.position + Game::PlayerRadius * circle[a], 0.0f),
				glm::vec3(game.player_1.position + Game::PlayerRadius * circle[(a+1)%circle.size()], 0.0f),
				col_1
			);
		}
		draw_text(glm::vec2(0.8f, 0.25f), std::to_string(game.player_1.score), 0.1f);

		for (auto const &puck : game.pucks) {
			glm::u8vec4 col = get_color(puck.last_hit);
			for (uint32_t a = 0; a < circle.size(); ++a) {
				lines.draw(
					glm::vec3(puck.position + Game::PuckRadius * circle[a], 0.0f),
					glm::vec3(puck.position + Game::PuckRadius * circle[(a+1)%circle.size()], 0.0f),
					col
				);
			}
		}

		if (game.grace_period > 0.0f) {
			int num = static_cast< int >(std::ceil(game.grace_period));
			draw_text(glm::vec2(-0.1f, -0.2f), std::to_string(num), 0.5f);
		}
	}
	GL_ERRORS();
}
